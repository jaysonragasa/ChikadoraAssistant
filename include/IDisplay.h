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
    
    // Call in the main loop to handle non-blocking animations
    virtual void update() = 0; 
};
