#pragma once

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual void initialize() = 0;
    virtual void showIdle() = 0;
    virtual void showListening() = 0;
    virtual void showSpeaking() = 0;
    virtual void triggerBlink() = 0;
    
    // Call in the main loop to handle non-blocking animations
    virtual void update() = 0; 
};
