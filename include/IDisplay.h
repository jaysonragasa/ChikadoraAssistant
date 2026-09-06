#pragma once
#include <stdint.h>
#include "WeatherData.h"

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual void initialize() = 0;
    virtual void showIdle() = 0;
    virtual void showListening() = 0;
    virtual void showThinking() = 0;   // "sending" - eyes shut
    virtual void showSpeaking() = 0;
    virtual void triggerBlink() = 0;
    virtual void shakeEyes() = 0;      // brief "I didn't catch that" shake

    // ---- Boot / connectivity screens ----
    // "Connecting to: <ssid>" with an animated spinner.
    virtual void showConnecting(const char* ssid) = 0;
    // "Connected" + the device IP address (static, shown briefly before the face).
    virtual void showConnected(const char* ip) = 0;
    // "Connecting to TTS server: <host>" with an animated spinner.
    virtual void showConnectingServer(const char* host) = 0;

    // Switch to the weather page (page 2). Rendered from the latest weather data
    // (see setWeather) with animated icons.
    virtual void showWeather() = 0;

    // Provide the latest weather data for the weather page. Refreshes the page
    // if it's currently showing.
    virtual void setWeather(const WeatherData& data) = 0;
    // Failed/retrying: closed "sleeping" eyes bobbing up/down + a corner spinner.
    virtual void showSleeping() = 0;

    // Toggle a small "!TTS" badge in the top-right of the face, shown when the
    // TTS/LLM server couldn't be reached (running on default settings).
    virtual void setServerWarning(bool show) = 0;

    // Set the face/eye geometry. Applied immediately (used for live preview from
    // the web config page and at boot from stored settings).
    virtual void setEyeConfig(int width, int height, int gap, int roundness, bool showMouth) = 0;

    // Which top-left status overlays to draw (behind the eyes).
    virtual void setOverlayFlags(bool showLoopTime, bool showFreeMem) = 0;
    // Latest metrics for those overlays: main-loop frequency (Hz) and free heap.
    virtual void setMetrics(float loopHz, uint32_t freeHeapBytes) = 0;
    
    // Call in the main loop to handle non-blocking animations
    virtual void update() = 0; 
};
