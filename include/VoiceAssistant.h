#pragma once
#include <Arduino.h>

#include "IDisplay.h"
#include "ITouchSensor.h"
#include "IAudioInput.h"
#include "IAudioOutput.h"
#include "IApiClient.h"
#include "IWeatherProvider.h"
#include "WeatherData.h"
#include "Config.h"
#include "RuntimeSettings.h"
#include "WebConfigServer.h"
#include "TapDetector.h"

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
                   IApiClient& api,
                   IWeatherProvider& weather);

    void begin();   // one-time bring-up (call from setup())
    void loop();    // run the state machine (call from loop())

private:
    enum class State { Idle, Listening, Processing, Speaking };

    // Which page the display is showing. 1 tap speaks; 2 taps switch page.
    enum class Page { Eyes, Weather };

    // Injected collaborators.
    IDisplay&         display;
    ITouchSensor&     touch;
    IAudioInput&      mic;
    IAudioOutput&     speaker;
    IApiClient&       api;
    IWeatherProvider& weather;

    // Web-configurable settings, seeded from Config defaults and refreshed from
    // the server at boot (see begin()).
    RuntimeSettings settings;

    // Device-hosted config page (set the TTS server IP/port from a browser).
    WebConfigServer webConfig;

    // Touch paging: turns raw touches into single/double taps and tracks page.
    TapDetector tapDetector;
    Page        page = Page::Eyes;

    // Runtime state.
    State  state = State::Idle;
    String currentJobId;
    unsigned long lastJobPollAt = 0;   // non-blocking TTS job polling

    // Main-loop frequency + free-heap sampling for the top-left overlays.
    unsigned long loopCount = 0;
    unsigned long lastMetricAt = 0;

    // Weather polling.
    WeatherData   weatherData;
    unsigned long lastWeatherPollAt = 0;
    bool          weatherPolled = false;
    bool          speakingWeather = false;   // true while speaking a weather summary
    bool          agentSpeaking = false;     // true while agent is speaking

    // Chunked-playback queue: a TTS job id per clip, plus which one we're on.
    String clipJobIds[Config::Playback::MAX_CLIPS];
    int    clipCount = 0;
    int    clipIndex = 0;

    // State handlers.
    void handleGesture(TapDetector::Event ev);   // dispatch tap/long-press
    void handleProcessing();
    void handleSpeaking();

    // Paging + speak helpers.
    void startListening();   // begin the tap->record->...->speak flow
    void stopAndProcess();   // finish a recording and kick off transcription
    void stopSpeaking();     // abort any in-progress playback/queue
    void togglePage();       // switch between the Eyes and Weather pages
    void pollWeatherIfDue(unsigned long now);   // refresh the forecast periodically
    void   speakWeather();       // speak a summary of the current forecast
    String weatherSummary();     // build the spoken weather summary text
    String timeDatePhrase();     // spoken "It is <time> on <date>" (empty if unsynced)
    void   concludeSpeaking();   // return to the right page when a reply finishes

    // Helpers.
    String think(const String& heard);        // LLM reply or echo/fallback
    bool   queueReply(const String& text);     // split + submit TTS jobs
    void   goIdle();
};
