#include "VoiceAssistant.h"
#include "WifiConnector.h"
#include "TextChunker.h"
#include "DeviceConfig.h"
#include <time.h>
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
    settings = DeviceConfig::settings();
    api.configure(DeviceConfig::serverIp(), settings);
    display.setEyeConfig(settings.eyeWidth, settings.eyeHeight, settings.eyeGap,
                         settings.eyeRoundness, settings.showMouth);
    display.setOverlayFlags(settings.showLoopTime, settings.showFreeMem);
    tapDetector.setHoldMs(settings.holdMs);

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
    
    // Wire up Agent Extension callbacks
    webConfig.onAgentStart = [this]() {
        agentSpeaking = true;
        if (queueReply("Agent " + settings.agentName + " ready to serve!")) {
            state = State::Processing;
        }
    };
    webConfig.onAgentThinking = [this]() {
        display.showAgentThinking();
        state = State::Idle;
    };
    webConfig.onAgentEnd = [this]() {
        agentSpeaking = true;
        if (queueReply("Done working!")) {
            state = State::Processing;
        }
    };
    webConfig.onAgentSpeak = [this](const String& text) {
        agentSpeaking = true;
        if (text.length() > 0) {
            display.showAgentSpeaking();
            if (queueReply(text)) {
                state = State::Processing;
            }
        }
    };

    OtaUpdater::begin(Config::Ota::HOSTNAME, Config::Ota::PASSWORD);
    // Sync time from NTP (UTC). The weather page converts it to local time using
    // Open-Meteo's utc_offset_seconds for the configured coordinates.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
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
    ServerStatus srvStatus;
    while (millis() - sstart < SERVER_CONNECT_MS) {
        display.update();               // animate the spinner
        srvStatus = api.ping();
        if (srvStatus.ttsReachable) { serverUp = true; break; }
        delay(50);
    }

    // The ping is only for the "!TTS" badge now - settings already came from NVS.
    if (serverUp) {
        Serial.println("Server reachable.");
    } else {
        Serial.println("Server not reachable after timeout.");
    }
    display.setServerStatus(srvStatus);   // show offline badges (!TTS, !STT, !O) on the face
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
    webConfig.handle();   // serve the config page
    OtaUpdater::handle(); // accept OTA firmware pushes

    // Sample touch every iteration in EVERY state, so a long-press can interrupt
    // speech/processing and switch pages.
    handleGesture(tapDetector.update(touch.isTouched(), now));

    // Per-state work (no touch reading here).
    switch (state) {
        case State::Listening:
            mic.update();
            if (!mic.isRecording()) stopAndProcess();   // buffer filled -> process
            break;
        case State::Processing: handleProcessing(); break;
        case State::Speaking:   handleSpeaking();   break;
        case State::Idle:       pollWeatherIfDue(now); break;
    }

    if (state != State::Speaking) {
        delay(5);
    }
}

// Dispatch a touch gesture. Tap = speak (and page-appropriate actions);
// LongPress = interrupt anything in progress and switch page.
void VoiceAssistant::handleGesture(TapDetector::Event ev) {
    if (ev == TapDetector::Event::LongPress) {
        Serial.println("Long press - switching page.");
        if (state == State::Speaking || state == State::Processing) stopSpeaking();
        if (state == State::Listening) { mic.stopRecording(); mic.releaseBuffer(); }
        state = State::Idle;
        togglePage();
    } else if (ev == TapDetector::Event::Tap) {
        if (state == State::Idle)            startListening();
        else if (state == State::Listening)  stopAndProcess();
        // Speaking / Processing: taps are ignored (long-press to interrupt).
    }
}

void VoiceAssistant::stopSpeaking() {
    speaker.stop();
    clipCount = 0;
    clipIndex = 0;
    speakingWeather = false;
}

void VoiceAssistant::pollWeatherIfDue(unsigned long now) {
    if (!settings.weatherEnabled) return;
    unsigned long interval = (unsigned long)settings.weatherPollMinutes * 60000UL;
    if (weatherPolled && (now - lastWeatherPollAt) < interval) return;

    Serial.println("[Weather] Polling Open-Meteo...");
    if (weather.fetch(settings.weatherLat, settings.weatherLon, weatherData)) {
        display.setWeather(weatherData);
        // If the user is looking at the weather page, speak the fresh forecast.
        if (page == Page::Weather) speakWeather();
    }
    // Advance the timer even on failure so we don't hammer the API on errors.
    lastWeatherPollAt = now;
    weatherPolled = true;
}

// Spoken "It is <time> on <weekday, month day>." Empty until NTP has synced.
String VoiceAssistant::timeDatePhrase() {
    time_t utc = time(nullptr);
    if (utc < 1700000000UL) return "";                 // clock not synced yet
    time_t local = utc + weatherData.utcOffsetSeconds;
    struct tm tmv;
    gmtime_r(&local, &tmv);

    char date[32];
    strftime(date, sizeof(date), "%A, %B %e", &tmv);   // "Wednesday, August 26"
    int h = tmv.tm_hour;
    const char* ap = h < 12 ? "AM" : "PM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    char t[16];
    snprintf(t, sizeof(t), "%d:%02d %s", h12, tmv.tm_min, ap);
    return "It is " + String(t) + " on " + String(date) + ". ";
}

// Build a spoken summary: time/date + current conditions + the next few hours.
String VoiceAssistant::weatherSummary() {
    const WeatherData& w = weatherData;
    String s = timeDatePhrase();
    s += "Current weather is " + String((int)roundf(w.currentTemp)) +
         " degrees with " + weatherConditionText(w.currentCode) + ". ";
    int n = w.hourCount < 4 ? w.hourCount : 4;
    if (n > 0) {
        s += "Hourly forecast: ";
        for (int i = 0; i < n; i++) {
            int h = w.hours[i].hour;
            const char* ap = h < 12 ? "AM" : "PM";
            int h12 = h % 12;
            if (h12 == 0) h12 = 12;
            s += String(h12) + " " + ap + ", " +
                 String((int)roundf(w.hours[i].temp)) + " degrees " +
                 weatherConditionText(w.hours[i].code);
            s += (i < n - 1) ? ". " : ".";
        }
    }
    return s;
}

void VoiceAssistant::speakWeather() {
    if (state != State::Idle || !weatherData.valid) return;   // don't interrupt

    String summary = weatherSummary();
    String toSpeak = summary;

    // Ask the LLM for a personality-flavored take + suggestion (the server's
    // /api/chat injects the TARS personality settings). Falls back to just the
    // factual forecast if the LLM is off or unreachable.
    if (settings.useLlm) {
        String prompt = "Here is today's weather. " + summary +
                        " In one or two short sentences, give your take on this "
                        "weather and suggest what I should do today.";
        Serial.println("[Weather] Asking the LLM for a take...");
        String take = api.chat(prompt);
        if (take.length() > 0) {
            Serial.println("[Weather] LLM: " + take);
            toSpeak += " " + take;
        }
    }

    Serial.println("[Weather] Speaking: " + toSpeak);
    if (queueReply(toSpeak)) {
        speakingWeather = true;
        state = State::Processing;   // the poller plays the clips
    }
}

// Return to the right page after a spoken reply finishes: the weather page for
// a weather summary, otherwise the idle face.
void VoiceAssistant::concludeSpeaking() {
    agentSpeaking = false;
    if (speakingWeather) {
        speakingWeather = false;
        display.showWeather();
        state = State::Idle;
    } else {
        goIdle();
    }
}

void VoiceAssistant::goIdle() {
    display.showIdle();
    state = State::Idle;
}

void VoiceAssistant::startListening() {
    Serial.println("Tap - starting recording...");
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
        weatherPolled = false;   // force a fresh poll so it fetches + speaks now
    } else {
        page = Page::Eyes;
        Serial.println("Double tap - switching to Eyes page.");
        display.showIdle();
    }
}

void VoiceAssistant::stopAndProcess() {
    Serial.println("Stopping recording and processing...");
    mic.stopRecording();
    display.showThinking();     // eyes shut while sending/thinking
    state = State::Processing;
    lastJobPollAt = 0;

    String heard = api.transcribeAudio(mic.getAudioData(), mic.getAudioSize());
    Serial.println("Heard: " + heard);

    if (heard.length() > 0) {
        // If we reached here, the server is up and STT is working.
        // Update the badges by pinging (very fast now)
        display.setServerStatus(api.ping());
    }

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
    // Poll the job at most ~2x/second, but return immediately otherwise so the
    // loop keeps sampling touch (a long-press can interrupt).
    if (millis() - lastJobPollAt < 500) return;
    lastJobPollAt = millis();

    int status = api.isTtsJobDone(currentJobId);
    if (status == 1) {
        Serial.println("TTS Job done! Playing audio...");
        String url = api.getAudioUrl(currentJobId);
        speaker.playUrl(url.c_str());
        if (agentSpeaking) {
            display.showAgentSpeaking();
        } else if (!speakingWeather) {
            display.showSpeaking();   // keep the weather page if summarizing
        }
        state = State::Speaking;
    } else if (status == -1) {
        Serial.println("[Error] TTS Job failed or returned 404. Canceling...");
        concludeSpeaking();
    }
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
        concludeSpeaking();
    }
}
