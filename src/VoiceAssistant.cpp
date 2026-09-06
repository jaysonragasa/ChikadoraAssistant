#include "VoiceAssistant.h"
#include "WifiConnector.h"
#include "TextChunker.h"
#include "DeviceConfig.h"
#include "OtaUpdater.h"

VoiceAssistant::VoiceAssistant(IDisplay& display,
                               ITouchSensor& touch,
                               IAudioInput& mic,
                               IAudioOutput& speaker,
                               IApiClient& api,
                               IWeatherProvider& weather)
    : display(display), touch(touch), mic(mic), speaker(speaker), api(api), weather(weather) {}

void VoiceAssistant::begin() {
    Serial.begin(115200);
    delay(3000); // let USB CDC enumerate so we don't miss early logs
    Serial.println("\n\n--- CHIKADORA ASSISTANT BOOTING ---");

    // Display first, so there's visual feedback during the rest of bring-up.
    display.initialize();

    // Load persisted config from NVS (single source of truth): TTS server
    // address + all runtime settings. Point the client at the server and keep
    // the settings for applying to the hardware below.
    DeviceConfig::begin();
    api.setServer(DeviceConfig::serverIp(), DeviceConfig::serverPort());
    settings = DeviceConfig::settings();
    api.setVoiceId(settings.voiceId);
    display.setEyeConfig(settings.eyeWidth, settings.eyeHeight, settings.eyeGap,
                         settings.eyeRoundness, settings.showMouth);
    display.setOverlayFlags(settings.showLoopTime, settings.showFreeMem);

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

    // 3) Connected: show "Connected / IP Address: ..." briefly. Start the
    //    device config page now so it's reachable at this IP.
    String ip = WifiConnector::ip();
    Serial.println("WiFi Connected! IP: " + ip);
    webConfig.begin(display);
    OtaUpdater::begin(Config::Ota::HOSTNAME, Config::Ota::PASSWORD);
    display.showConnected(ip.c_str());
    delay(1500);

    // 4) Connect to the TTS/LLM server - same visual treatment as Wi-Fi: a
    //    "Connecting to TTS server" screen with an animated spinner while we
    //    poll /api/health. Give up after a timeout and boot with defaults so
    //    the device still comes up if the server is down. The address comes
    //    from NVS (set via the config page), defaulting to Config.h.
    String host = DeviceConfig::serverIp() + ":" + String(DeviceConfig::serverPort());
    display.showConnectingServer(host.c_str());
    const unsigned long SERVER_CONNECT_MS = 30000;
    unsigned long sstart = millis();
    bool serverUp = false;
    while (millis() - sstart < SERVER_CONNECT_MS) {
        display.update();               // animate the spinner
        if (api.ping()) { serverUp = true; break; }
        delay(50);
    }

    // The ping is only for the "!TTS" badge now - settings already came from NVS.
    if (serverUp) {
        Serial.println("Server reachable.");
    } else {
        Serial.println("Server not reachable after timeout.");
    }
    display.setServerWarning(!serverUp);   // show "!TTS" badge on the face if offline
    Serial.printf("Settings: voice=%s micGain=%.2f vol=%.2f useLlm=%d noSpeech=%d mode=%s wpc=%d\n",
                  settings.voiceId.c_str(), settings.micGain, settings.speakerVolume,
                  settings.useLlm, settings.speakOnNoSpeech,
                  settings.playbackMode == Config::PlaybackMode::Stream ? "stream" : "chunk",
                  settings.wordsPerChunk);
    Serial.printf("Overlays: loopTime=%d freeMem=%d | Eyes: w=%d h=%d gap=%d r=%d mouth=%d | "
                  "Weather: en=%d %.4f,%.4f every %dm\n",
                  settings.showLoopTime, settings.showFreeMem,
                  settings.eyeWidth, settings.eyeHeight, settings.eyeGap,
                  settings.eyeRoundness, settings.showMouth,
                  settings.weatherEnabled, settings.weatherLat, settings.weatherLon,
                  settings.weatherPollMinutes);

    // Bring up the rest of the hardware while the user reads the connected screen.
    touch.initialize();

    mic.initialize();
    mic.setGain(settings.micGain);

    speaker.initialize();
    speaker.setStreamingMode(settings.playbackMode == Config::PlaybackMode::Stream);
    speaker.setVolume(settings.speakerVolume);

    display.showIdle();     // now show the eyes

    if (serverUp) {
        Serial.println("System Ready. Tap to listen!");
    } else {
        Serial.println("System up, but the TTS/STT server is OFFLINE (see !TTS on screen).");
        Serial.println("Tapping won't do anything useful until it's reachable. Set the");
        Serial.println("server address at http://" + ip + "/ then restart.");
    }
}

void VoiceAssistant::loop() {
    // Sample the main-loop frequency + free heap once per second for the
    // top-left overlays (cheap; the display only redraws if a value changed).
    loopCount++;
    unsigned long now = millis();
    if (now - lastMetricAt >= 1000) {
        float hz = loopCount * 1000.0f / (now - lastMetricAt);
        display.setMetrics(hz, ESP.getFreeHeap());
        loopCount = 0;
        lastMetricAt = now;
    }

    display.update();
    webConfig.handle();   // serve the config page (responsive when idle/listening)
    OtaUpdater::handle(); // accept OTA firmware pushes when idle

    // Speaking/Processing run to the exclusion of touch handling.
    if (state == State::Speaking)   { handleSpeaking();   return; }
    if (state == State::Processing) { handleProcessing(); return; }

    // Refresh the forecast in the background while idle (the fetch blocks
    // briefly, so we avoid doing it mid-interaction).
    if (state == State::Idle) pollWeatherIfDue(now);

    const bool touched = touch.isTouched();
    if (state == State::Idle)           handleIdle(touched, now);
    else if (state == State::Listening) handleListening(touched);

    wasTouched = touched;
    delay(10);
}

void VoiceAssistant::pollWeatherIfDue(unsigned long now) {
    if (!settings.weatherEnabled) return;
    unsigned long interval = (unsigned long)settings.weatherPollMinutes * 60000UL;
    if (weatherPolled && (now - lastWeatherPollAt) < interval) return;

    Serial.println("[Weather] Polling Open-Meteo...");
    if (weather.fetch(settings.weatherLat, settings.weatherLon, weatherData)) {
        display.setWeather(weatherData);
    }
    // Advance the timer even on failure so we don't hammer the API on errors.
    lastWeatherPollAt = now;
    weatherPolled = true;
}

void VoiceAssistant::goIdle() {
    display.showIdle();
    state = State::Idle;
}

void VoiceAssistant::handleIdle(bool touched, unsigned long now) {
    switch (tapDetector.update(touched, now)) {
        case TapDetector::Event::SingleTap: startListening(); break;
        case TapDetector::Event::DoubleTap: togglePage();     break;
        default: break;
    }
}

void VoiceAssistant::startListening() {
    Serial.println("Single tap - starting recording...");
    page = Page::Eyes;              // interaction always happens on the face
    display.showListening();
    speaker.playDingDong();         // audible feedback
    mic.startRecording();
    state = State::Listening;
}

void VoiceAssistant::togglePage() {
    if (page == Page::Eyes) {
        page = Page::Weather;
        Serial.println("Double tap - switching to Weather page.");
        display.showWeather();
    } else {
        page = Page::Eyes;
        Serial.println("Double tap - switching to Eyes page.");
        display.showIdle();
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
        if (settings.speakOnNoSpeech) {
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
    if (!settings.useLlm) return heard;   // echo mode

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
    if (settings.playbackMode == Config::PlaybackMode::Stream) {
        // Streaming plays any length, so send the whole reply as a single job.
        n = (text.length() > 0) ? 1 : 0;
        if (n == 1) clips[0] = text;
    } else {
        n = TextChunker::split(text, clips, Config::Playback::MAX_CLIPS,
                               settings.wordsPerChunk);
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
