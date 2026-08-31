#pragma once
#include <Arduino.h>

#include "IDisplay.h"
#include "ITrigger.h"
#include "ITouchSensor.h"
#include "IAudioInput.h"
#include "IAudioOutput.h"
#include "IApiClient.h"
#include "Config.h"

// Orchestrates the assistant: (tap or voice) -> record -> transcribe -> chat ->
// synthesize -> play. Depends only on abstractions injected via the constructor
// (Dependency Inversion). The ITrigger decides *how* a conversation starts
// (touch vs. voice); `touch` is kept for a manual stop while recording.
class VoiceAssistant {
public:
    VoiceAssistant(IDisplay& display,
                   ITrigger& trigger,
                   ITouchSensor& touch,
                   IAudioInput& mic,
                   IAudioOutput& speaker,
                   IApiClient& api);

    void begin();   // one-time bring-up (call from setup())
    void loop();    // run the state machine (call from loop())

private:
    enum class State { Idle, Listening, Processing, Speaking };

    // Injected collaborators.
    IDisplay&     display;
    ITrigger&     trigger;
    ITouchSensor& touch;
    IAudioInput&  mic;
    IAudioOutput& speaker;
    IApiClient&   api;

    // Runtime state.
    State  state = State::Idle;
    bool   wasTouched = false;   // for manual stop-tap edge detection
    String currentJobId;

    // Chunked-playback queue: a TTS job id per clip, plus which one we're on.
    String clipJobIds[Config::Playback::MAX_CLIPS];
    int    clipCount = 0;
    int    clipIndex = 0;

    // State handlers.
    void handleIdle();
    void handleListening();
    void handleProcessing();
    void handleSpeaking();

    // Helpers.
    String think(const String& heard);        // LLM reply or echo/fallback
    bool   queueReply(const String& text);     // split + submit TTS jobs
    void   goIdle();
};
