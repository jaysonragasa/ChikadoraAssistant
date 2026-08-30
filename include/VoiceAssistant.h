#pragma once
#include <Arduino.h>

#include "IDisplay.h"
#include "ITouchSensor.h"
#include "IAudioInput.h"
#include "IAudioOutput.h"
#include "IApiClient.h"
#include "Config.h"

// Orchestrates the assistant: tap -> record -> transcribe -> chat -> synthesize
// -> play. Depends only on the abstractions (interfaces), which are injected via
// the constructor (Dependency Inversion), so it knows nothing about the concrete
// OLED / mic / amp / HTTP client it drives.
class VoiceAssistant {
public:
    VoiceAssistant(IDisplay& display,
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
    ITouchSensor& touch;
    IAudioInput&  mic;
    IAudioOutput& speaker;
    IApiClient&   api;

    // Runtime state.
    State  state = State::Idle;
    bool   wasTouched = false;
    String currentJobId;

    // Chunked-playback queue: a TTS job id per clip, plus which one we're on.
    String clipJobIds[Config::Playback::MAX_CLIPS];
    int    clipCount = 0;
    int    clipIndex = 0;

    // State handlers.
    void handleIdle(bool touched);
    void handleListening(bool touched);
    void handleProcessing();
    void handleSpeaking();

    // Helpers.
    String think(const String& heard);        // LLM reply or echo/fallback
    bool   queueReply(const String& text);     // split + submit TTS jobs
    void   goIdle();
};
