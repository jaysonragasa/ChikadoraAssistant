#pragma once

// Abstraction for "how the user starts a conversation" (tap vs. voice). The
// VoiceAssistant polls triggered() while idle and doesn't care how the signal
// is produced. onEnterIdle/onExitIdle let a trigger start/stop any resources it
// needs while idle (e.g. mic monitoring for voice activation).
class ITrigger {
public:
    virtual ~ITrigger() = default;
    virtual void begin() {}        // one-time setup
    virtual void onEnterIdle() {}  // called when the assistant enters idle
    virtual void onExitIdle() {}   // called just before leaving idle
    virtual bool triggered() = 0;  // poll while idle: did the user start?
};
