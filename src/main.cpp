#include <Arduino.h>
#include <WiFi.h>

#include "IDisplay.h"
#include "OledFaceDisplay.h"
#include "ITouchSensor.h"
#include "Ttp223bSensor.h"
#include "IAudioInput.h"
#include "I2SMicrophone.h"
#include "IAudioOutput.h"
#include "I2SAmplifier.h"
#include "IApiClient.h"
#include "LocalTtsClient.h"

// WiFi Credentials
#define WIFI_SSID "Anikanik2G"
#define WIFI_PASS "QazWsx12345"

// Local TTS Server
#define SERVER_IP "192.168.1.60"
#define SERVER_PORT 8000
#define VOICE_ID "af_sarah"

// Microphone gain, as a clean multiplier over unity (1.0 = full 24-bit mic
// range mapped to 16-bit; no hidden extra gain). Raise if the assistant can't
// hear you, lower if speech clips/distorts. ~2-5 is typical for the INMP441.
#define MIC_GAIN 3.0f

// When 1, transcribed speech is sent to the LLM (via the server's /api/chat,
// backed by Ollama) and the reply is spoken. When 0, the assistant just echoes
// what it heard. Requires Ollama running + a model set in the web app Settings.
#define USE_LLM 1

// Playback strategy. Two options:
//   PLAYBACK_CHUNK  - chop the reply into small clips (WORDS_PER_CHUNK words),
//                     queue them all, and play each with buffered download-then-
//                     play. Smooth on the single-core C3. (Recommended.)
//   PLAYBACK_STREAM - send the whole reply as one job and stream it to I2S as it
//                     downloads. Handles any length but can stutter if the
//                     network hiccups.
#define PLAYBACK_CHUNK  0
#define PLAYBACK_STREAM 1
#define PLAYBACK_MODE   PLAYBACK_STREAM

// In chunk mode, the reply is split into clips of ~this many words each. All
// clips are queued up front so the server synthesizes the next while the
// current one plays, keeping the gaps between clips short.
#define WORDS_PER_CHUNK 10
#define MAX_CHUNKS 24

// Pinout
#define OLED_SDA 9
#define OLED_SCL 8
#define TOUCH_PIN 10
#define I2S_BCLK 2
#define I2S_LRC 3
#define AMP_DIN 4
#define MIC_DOUT 5

// State Machine
enum AppState {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_PROCESSING,
    STATE_SPEAKING
};

AppState currentState = STATE_IDLE;

// Dependencies
IDisplay* displayManager;

ITouchSensor* touchSensor;
IAudioInput* mic;
IAudioOutput* speaker;
IApiClient* apiClient;

bool wasTouched = false;
String currentJobId = "";

// Chunked-playback state: job ids for each clip, and which one we're on.
String chunkJobIds[MAX_CHUNKS];
int chunkCount = 0;
int chunkIndex = 0;

void setup() {
    Serial.begin(115200);
    
    // Give the USB CDC time to enumerate on the PC so we don't miss logs
    delay(3000);
    Serial.println("\n\n--- CHIKADORA ASSISTANT BOOTING ---");

    // 1. Initialize Display First (for feedback)
    displayManager = new OledFaceDisplay(OLED_SDA, OLED_SCL);
    displayManager->initialize();
    displayManager->showIdle();

    // 2. Connect to WiFi
    Serial.print("Connecting to WiFi");
    
    // Explicitly set to Station mode and clear old config
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    // Reduce WiFi transmission power to prevent voltage drops on the breadboard!
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    Serial.println("\n--- WiFi Scan ---");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("No networks found! (Is the antenna damaged?)");
    } else {
        Serial.printf("%d networks found:\n", n);
        for (int i = 0; i < n; ++i) {
            Serial.printf("%d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    Serial.println("-----------------");

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            Serial.print("\n[WiFi] Disconnected! Reason: ");
            Serial.println(info.wifi_sta_disconnected.reason);
        }
    });
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

    // 3. Initialize Sensors and Audio
    touchSensor = new Ttp223bSensor(TOUCH_PIN);
    touchSensor->initialize();

    mic = new I2SMicrophone(I2S_BCLK, I2S_LRC, MIC_DOUT);
    mic->initialize();
    mic->setGain(MIC_GAIN);

    speaker = new I2SAmplifier(I2S_BCLK, I2S_LRC, AMP_DIN);
    speaker->initialize();
    speaker->setStreamingMode(PLAYBACK_MODE == PLAYBACK_STREAM);

    apiClient = new LocalTtsClient(SERVER_IP, SERVER_PORT, VOICE_ID);

    Serial.println("System Ready. Tap to listen!");
}

// Split text into clips of at most WORDS_PER_CHUNK words, preferring to end a
// clip at sentence punctuation so the pauses fall at natural boundaries.
// Fills out[] and returns the number of clips (capped at maxChunks).
int splitIntoChunks(const String& text, String out[], int maxChunks) {
    int count = 0;
    String current = "";
    int words = 0;
    int i = 0;
    int n = text.length();

    while (i < n && count < maxChunks) {
        while (i < n && text[i] == ' ') i++;   // skip spaces
        if (i >= n) break;
        int start = i;
        while (i < n && text[i] != ' ') i++;    // read one word
        String word = text.substring(start, i);

        if (current.length() > 0) current += " ";
        current += word;
        words++;

        bool endsSentence = word.endsWith(".") || word.endsWith("!") ||
                            word.endsWith("?") || word.endsWith(".\"") ||
                            word.endsWith("!\"") || word.endsWith("?\"");

        if (words >= WORDS_PER_CHUNK || (endsSentence && words >= 3)) {
            out[count++] = current;
            current = "";
            words = 0;
        }
    }
    if (current.length() > 0 && count < maxChunks) {
        out[count++] = current;
    }
    return count;
}

// Chop `text` into clips and queue a TTS job for each, up front. Returns true
// if at least one clip was queued (currentJobId points at the first).
bool submitChunks(const String& text) {
    static String chunks[MAX_CHUNKS];
#if PLAYBACK_MODE == PLAYBACK_STREAM
    // Streaming plays any length, so send the whole reply as a single job.
    int n = (text.length() > 0) ? 1 : 0;
    if (n == 1) chunks[0] = text;
#else
    int n = splitIntoChunks(text, chunks, MAX_CHUNKS);
#endif
    if (n == 0) return false;

    Serial.printf("Split reply into %d clip(s).\n", n);
    int queued = 0;
    for (int i = 0; i < n; i++) {
        String id = apiClient->submitTtsJob(chunks[i]);
        if (id == "") {
            Serial.printf("[Clip %d] Failed to queue; stopping here.\n", i);
            break;
        }
        chunkJobIds[queued++] = id;
        Serial.printf("[Clip %d/%d] job %s: \"%s\"\n", i + 1, n, id.c_str(), chunks[i].c_str());
    }

    chunkCount = queued;
    chunkIndex = 0;
    if (chunkCount == 0) return false;
    currentJobId = chunkJobIds[0];
    return true;
}

void loop() {
    displayManager->update();

    if (currentState == STATE_SPEAKING) {
        static unsigned long lastDebug = 0;
        if (millis() - lastDebug > 1000) {
            speaker->debugAudio();
            lastDebug = millis();
        }
        
        speaker->update();
        if (!speaker->isPlaying()) {
            // Free this clip's buffer/I2S driver before moving on.
            speaker->stop();

            chunkIndex++;
            if (chunkIndex < chunkCount) {
                // The next clip was (likely) synthesized while this one played.
                // Go wait for/play it - keep the speaking face up.
                currentJobId = chunkJobIds[chunkIndex];
                Serial.printf("Next clip %d/%d (job %s)\n",
                              chunkIndex + 1, chunkCount, currentJobId.c_str());
                currentState = STATE_PROCESSING;
            } else {
                Serial.println("All clips played.");
                displayManager->showIdle();
                currentState = STATE_IDLE;
            }
        }
        return; // Don't process touch while speaking for now
    }

    if (currentState == STATE_PROCESSING) {
        // Poll for TTS Job
        int status = apiClient->isTtsJobDone(currentJobId);
        if (status == 1) {
            Serial.println("TTS Job done! Playing audio...");
            String url = apiClient->getAudioUrl(currentJobId);
            speaker->playUrl(url.c_str());
            displayManager->showSpeaking();
            currentState = STATE_SPEAKING;
        } else if (status == -1) {
            Serial.println("[Error] TTS Job failed or returned 404. Canceling...");
            displayManager->showIdle();
            currentState = STATE_IDLE;
        }
        delay(500); // Polling interval
        return;
    }

    bool currentlyTouched = touchSensor->isTouched();

    if (currentState == STATE_IDLE) {
        if (currentlyTouched && !wasTouched) {
            Serial.println("Touch detected! Starting recording...");
            displayManager->showListening();
            
            // Play "Ding-Dong" feedback
            speaker->playDingDong();
            
            mic->startRecording();
            currentState = STATE_LISTENING;
        }
    } 
    else if (currentState == STATE_LISTENING) {
        mic->update();
        
        // Stop if touched again OR if buffer gets full automatically
        if ((currentlyTouched && !wasTouched) || !mic->isRecording()) {
            Serial.println("Stopping recording and processing...");
            mic->stopRecording();
            
            // Show "Thinking" by triggering blink (or custom face)
            displayManager->triggerBlink(); 
            currentState = STATE_PROCESSING;

            // Transcribe Audio
            String text = apiClient->transcribeAudio(mic->getAudioData(), mic->getAudioSize());
            Serial.println("Heard: " + text);

            // The recorded PCM has been sent; free its 144 KB now so the audio
            // library has enough heap to buffer playback (prevents OOM crash).
            mic->releaseBuffer();
            Serial.printf("Free heap after releasing mic buffer: %u bytes\n", ESP.getFreeHeap());

            if (text.length() > 0) {
                // Decide what to speak: Gemini's reply, or a plain echo.
                String toSpeak = text;
#if USE_LLM
                Serial.println("Asking the LLM...");
                String reply = apiClient->chat(text);
                if (reply.length() > 0) {
                    Serial.println("LLM: " + reply);
                    toSpeak = reply;
                } else {
                    // Chat failed (no key, network, etc.) - speak a short notice
                    // so the failure is audible instead of silently echoing.
                    Serial.println("LLM reply empty; speaking a fallback notice.");
                    toSpeak = "Sorry, I couldn't reach my brain right now.";
                }
#endif
                // Chop the reply into small clips and queue them all up front.
                if (!submitChunks(toSpeak)) {
                    Serial.println("Failed to queue TTS job(s).");
                    displayManager->showIdle();
                    currentState = STATE_IDLE;
                }
                // On success we stay in STATE_PROCESSING (set above) and the
                // poller will play clips in order.
            } else {
                Serial.println("No text heard.");
                displayManager->showIdle();
                currentState = STATE_IDLE;
            }
        }
    }

    wasTouched = currentlyTouched;
    delay(10);
}
