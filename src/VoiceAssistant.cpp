#include "VoiceAssistant.h"
#include "WifiConnector.h"
#include "TextChunker.h"

VoiceAssistant::VoiceAssistant(IDisplay& display,
                               ITouchSensor& touch,
                               IAudioInput& mic,
                               IAudioOutput& speaker,
                               IApiClient& api)
    : display(display), touch(touch), mic(mic), speaker(speaker), api(api) {}

void VoiceAssistant::begin() {
    Serial.begin(115200);
    delay(3000); // let USB CDC enumerate so we don't miss early logs
    Serial.println("\n\n--- CHIKADORA ASSISTANT BOOTING ---");

    // Display first, so there's visual feedback during the rest of bring-up.
    display.initialize();

    // 1) "Connecting to: <ssid>" with a spinner while the first attempt runs.
    display.showConnecting(Config::Wifi::SSID);
    WifiConnector::begin(Config::Wifi::SSID, Config::Wifi::PASS);

    const unsigned long FIRST_ATTEMPT_MS = 20000;   // grace period before "sleeping"
    unsigned long start = millis();
    while (!WifiConnector::isConnected() && millis() - start < FIRST_ATTEMPT_MS) {
        display.update();   // animate the connecting spinner
        delay(50);
    }

    // 2) Still not up? Show sleeping eyes + corner spinner and keep retrying.
    if (!WifiConnector::isConnected()) {
        Serial.println("First attempt timed out; showing sleep screen and retrying...");
        display.showSleeping();
        unsigned long lastRetry = millis();
        while (!WifiConnector::isConnected()) {
            display.update();               // bob eyes + spin
            if (millis() - lastRetry > 15000) {   // nudge a stalled retry
                WifiConnector::retry(Config::Wifi::SSID, Config::Wifi::PASS);
                lastRetry = millis();
            }
            delay(50);
        }
    }

    // 3) Connected: show "Connected / IP Address: ..." for ~3s, then the face.
    String ip = WifiConnector::ip();
    Serial.println("WiFi Connected! IP: " + ip);
    display.showConnected(ip.c_str());

    // Bring up the rest of the hardware while the user reads the connected screen.
    touch.initialize();

    mic.initialize();
    mic.setGain(Config::Audio::MIC_GAIN);

    speaker.initialize();
    speaker.setStreamingMode(Config::Playback::MODE == Config::PlaybackMode::Stream);
    speaker.setVolume(Config::Audio::SPEAKER_VOLUME);

    delay(3000);            // keep the connected screen up briefly
    display.showIdle();     // now show the eyes

    Serial.println("System Ready. Tap to listen!");
}

void VoiceAssistant::loop() {
    display.update();

    // Speaking/Processing run to the exclusion of touch handling.
    if (state == State::Speaking)   { handleSpeaking();   return; }
    if (state == State::Processing) { handleProcessing(); return; }

    const bool touched = touch.isTouched();
    if (state == State::Idle)           handleIdle(touched);
    else if (state == State::Listening) handleListening(touched);

    wasTouched = touched;
    delay(10);
}

void VoiceAssistant::goIdle() {
    display.showIdle();
    state = State::Idle;
}

void VoiceAssistant::handleIdle(bool touched) {
    if (touched && !wasTouched) {
        Serial.println("Touch detected! Starting recording...");
        display.showListening();
        speaker.playDingDong();     // audible feedback
        mic.startRecording();
        state = State::Listening;
    }
}

void VoiceAssistant::handleListening(bool touched) {
    mic.update();

    // Stop on a second tap, or when the mic buffer fills automatically.
    const bool tappedAgain = touched && !wasTouched;
    if (!tappedAgain && mic.isRecording()) return;

    Serial.println("Stopping recording and processing...");
    mic.stopRecording();
    display.showThinking();     // eyes shut while sending/thinking
    state = State::Processing;

    String heard = api.transcribeAudio(mic.getAudioData(), mic.getAudioSize());
    Serial.println("Heard: " + heard);

    // Free the recording's ~144 KB now so playback has heap to work with.
    mic.releaseBuffer();
    Serial.printf("Free heap after releasing mic buffer: %u bytes\n", ESP.getFreeHeap());

    if (heard.length() == 0) {
        Serial.println("No text heard.");
        display.shakeEyes();                       // visual "huh?"
        if (Config::Audio::SPEAK_ON_NO_SPEECH) {
            // Speak a retry prompt; on success stay in Processing to play it.
            if (queueReply(Config::Messages::NO_SPEECH)) return;
            Serial.println("Couldn't queue the retry prompt.");
        }
        goIdle();   // disabled, or TTS couldn't be queued
        return;
    }

    if (!queueReply(think(heard))) {
        Serial.println("Failed to queue TTS job(s).");
        goIdle();
    }
    // On success we stay in Processing; the poller plays clips in order.
}

String VoiceAssistant::think(const String& heard) {
    if (!Config::Audio::USE_LLM) return heard;   // echo mode

    Serial.println("Asking the LLM...");
    String reply = api.chat(heard);
    if (reply.length() > 0) {
        Serial.println("LLM: " + reply);
        return reply;
    }
    // Chat failed (server down, no model, etc.) - say something audible.
    Serial.println("LLM reply empty; speaking a fallback notice.");
    return Config::Messages::LLM_ERROR;
}

bool VoiceAssistant::queueReply(const String& text) {
    static String clips[Config::Playback::MAX_CLIPS];

    int n;
    if (Config::Playback::MODE == Config::PlaybackMode::Stream) {
        // Streaming plays any length, so send the whole reply as a single job.
        n = (text.length() > 0) ? 1 : 0;
        if (n == 1) clips[0] = text;
    } else {
        n = TextChunker::split(text, clips, Config::Playback::MAX_CLIPS,
                               Config::Playback::WORDS_PER_CHUNK);
    }
    if (n == 0) return false;

    Serial.printf("Split reply into %d clip(s).\n", n);
    int queued = 0;
    for (int i = 0; i < n; i++) {
        String id = api.submitTtsJob(clips[i]);
        if (id == "") {
            Serial.printf("[Clip %d] Failed to queue; stopping here.\n", i);
            break;
        }
        clipJobIds[queued++] = id;
        Serial.printf("[Clip %d/%d] job %s: \"%s\"\n", i + 1, n, id.c_str(), clips[i].c_str());
    }

    clipCount = queued;
    clipIndex = 0;
    if (clipCount == 0) return false;
    currentJobId = clipJobIds[0];
    return true;
}

void VoiceAssistant::handleProcessing() {
    int status = api.isTtsJobDone(currentJobId);
    if (status == 1) {
        Serial.println("TTS Job done! Playing audio...");
        String url = api.getAudioUrl(currentJobId);
        speaker.playUrl(url.c_str());
        display.showSpeaking();
        state = State::Speaking;
    } else if (status == -1) {
        Serial.println("[Error] TTS Job failed or returned 404. Canceling...");
        goIdle();
    }
    delay(500); // polling interval
}

void VoiceAssistant::handleSpeaking() {
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 1000) {
        speaker.debugAudio();
        lastDebug = millis();
    }

    speaker.update();
    if (speaker.isPlaying()) return;

    // This clip finished; free it and advance to the next queued clip.
    speaker.stop();
    clipIndex++;
    if (clipIndex < clipCount) {
        currentJobId = clipJobIds[clipIndex];
        Serial.printf("Next clip %d/%d (job %s)\n",
                      clipIndex + 1, clipCount, currentJobId.c_str());
        state = State::Processing;   // keep the speaking face; poll the next clip
    } else {
        Serial.println("All clips played.");
        goIdle();
    }
}
