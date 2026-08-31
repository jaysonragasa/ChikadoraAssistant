#include "VoiceAssistant.h"
#include "WifiConnector.h"
#include "TextChunker.h"

VoiceAssistant::VoiceAssistant(IDisplay& display,
                               ITrigger& trigger,
                               ITouchSensor& touch,
                               IAudioInput& mic,
                               IAudioOutput& speaker,
                               IApiClient& api)
    : display(display), trigger(trigger), touch(touch),
      mic(mic), speaker(speaker), api(api) {}

void VoiceAssistant::begin() {
    Serial.begin(115200);
    delay(3000); // let USB CDC enumerate so we don't miss early logs
    Serial.println("\n\n--- CHIKADORA ASSISTANT BOOTING ---");

    // Display first, so there's visual feedback during the rest of bring-up.
    display.initialize();
    display.showIdle();

    WifiConnector::connect(Config::Wifi::SSID, Config::Wifi::PASS);

    trigger.begin();   // sets up touch and any trigger resources

    mic.initialize();
    mic.setGain(Config::Audio::MIC_GAIN);
    // In voice mode, let recording end itself on trailing silence.
    if (Config::Trigger::MODE == Config::TriggerMode::Voice) {
        mic.setSilenceStop(true, Config::Trigger::SILENCE_THRESHOLD,
                           Config::Trigger::SILENCE_MS, Config::Trigger::MIN_SPEECH_MS);
    }

    speaker.initialize();
    speaker.setStreamingMode(Config::Playback::MODE == Config::PlaybackMode::Stream);
    speaker.setVolume(Config::Audio::SPEAKER_VOLUME);

    const bool voice = (Config::Trigger::MODE == Config::TriggerMode::Voice);
    Serial.println(voice ? "System Ready. Say something (or tap)!"
                         : "System Ready. Tap to listen!");
    trigger.onEnterIdle();  // begin idle monitoring (voice) / noop (touch)
    if (Config::Trigger::MODE == Config::TriggerMode::Voice) speaker.idleMute();
}

void VoiceAssistant::loop() {
    display.update();

    if (state == State::Speaking)   { handleSpeaking();   return; }
    if (state == State::Processing) { handleProcessing(); return; }
    if (state == State::Idle)       { handleIdle();       }
    else if (state == State::Listening) { handleListening(); }

    delay(10);
}

void VoiceAssistant::goIdle() {
    display.showIdle();
    state = State::Idle;
    trigger.onEnterIdle();   // resume monitoring for the next start
    if (Config::Trigger::MODE == Config::TriggerMode::Voice) speaker.idleMute();
}

void VoiceAssistant::handleIdle() {
    if (!trigger.triggered()) return;

    trigger.onExitIdle();   // stop monitoring so the port is free for record/tone
    Serial.println("Triggered! Starting recording...");
    display.showListening();

    // A feedback tone makes sense for touch; in voice mode it would talk over
    // the user (who's already speaking), so skip it there.
    if (Config::Trigger::MODE == Config::TriggerMode::Touch) {
        speaker.playDingDong();
    }

    mic.startRecording();
    wasTouched = touch.isTouched();  // seed so a held finger isn't seen as a stop
    state = State::Listening;
}

void VoiceAssistant::handleListening() {
    mic.update();

    // Stop on a manual tap, or when the mic stops itself (buffer full, or
    // trailing silence in voice mode).
    bool tNow = touch.isTouched();
    bool tapStop = tNow && !wasTouched;
    wasTouched = tNow;
    if (!tapStop && mic.isRecording()) return;

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
        Serial.println("No text heard; speaking a retry prompt.");
        display.shakeEyes();                       // visual "huh?"
        if (!queueReply(Config::Messages::NO_SPEECH)) {
            goIdle();   // couldn't even queue TTS (server down) - just go idle
        }
        // On success we stay in Processing and speak the prompt.
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
