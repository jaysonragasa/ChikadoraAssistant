#pragma once

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
    // Failed/retrying: closed "sleeping" eyes bobbing up/down + a corner spinner.
    virtual void showSleeping() = 0;
    
    // Call in the main loop to handle non-blocking animations
    virtual void update() = 0; 
};
