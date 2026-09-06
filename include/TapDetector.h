#pragma once
#include <Arduino.h>

// Turns a raw pressed/released touch signal into tap gestures.
//
// A release (press then let go) counts as one tap. Two taps within `windowMs`
// are a DoubleTap; a lone tap becomes a SingleTap once the window elapses (so
// SingleTap is intentionally delayed by up to windowMs to disambiguate).
//
// Poll update() every loop with the current press state and time. Pure logic,
// no I/O - a single responsibility that's easy to reason about and test.
class TapDetector {
public:
    enum class Event { None, SingleTap, DoubleTap };

    Event update(bool pressed, unsigned long now) {
        Event ev = Event::None;

        // Release edge = one tap.
        if (!pressed && wasPressed) {
            pendingTaps++;
            lastReleaseAt = now;
            if (pendingTaps >= 2) {
                ev = Event::DoubleTap;
                pendingTaps = 0;
            }
        }
        // A single pending tap resolves once the double-tap window passes.
        if (pendingTaps == 1 && (now - lastReleaseAt) > windowMs) {
            ev = Event::SingleTap;
            pendingTaps = 0;
        }

        wasPressed = pressed;
        return ev;
    }

    void reset() { wasPressed = false; pendingTaps = 0; }

private:
    static const unsigned long windowMs = 400;   // double-tap window
    bool          wasPressed   = false;
    int           pendingTaps  = 0;
    unsigned long lastReleaseAt = 0;
};
