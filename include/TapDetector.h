#pragma once
#include <Arduino.h>

// Turns a raw, bouncy/pulsing touch signal into two gestures:
//   Tap       - a normal press then release            -> "speak"
//   LongPress - the pad held for `holdMs` continuously  -> "switch page"
//
// The TTP223B output isn't a clean steady HIGH while held - it dips LOW briefly
// and repeatedly. A naive debounce treats each dip as a release, firing a tap on
// every dip (so it "starts listening over and over" during a hold). The fix is
// an ASYMMETRIC debounce:
//   * a press is registered immediately (first HIGH), and
//   * a release is only registered after the signal stays LOW continuously for
//     `releaseMs` - so short dips during a hold are bridged and the press reads
//     as one continuous hold.
//
// A Tap fires on the confirmed release (about `releaseMs` after you lift); a
// LongPress fires while still held, once the press crosses `holdMs`, and the
// eventual release then produces no Tap.
//
// Poll update() every loop with the raw press state and current time.
class TapDetector {
public:
    enum class Event { None, Tap, LongPress };

    // Hold duration (ms) that triggers a LongPress. Default 2000 ms (2 s).
    void setHoldMs(unsigned long ms) { holdMs = ms; }

    Event update(bool rawPressed, unsigned long now) {
        if (rawPressed) {
            lastHigh = now;                       // remember the last HIGH sample
            if (!held) {                          // press begins on the first HIGH
                held = true;
                pressStart = now;
                longFired = false;
            }
        } else if (held && (now - lastHigh) >= releaseMs) {
            // LOW has persisted long enough to count as a real release.
            held = false;
            unsigned long heldFor = lastHigh - pressStart;   // touched duration
            if (!longFired && heldFor >= minTapMs) {
                return Event::Tap;
            }
        }

        // Held continuously past the threshold -> one LongPress (fires while held).
        if (held && !longFired && (now - pressStart) >= holdMs) {
            longFired = true;
            return Event::LongPress;
        }
        return Event::None;
    }

    void reset() {
        held = false;
        longFired = false;
    }

private:
    static const unsigned long releaseMs = 200;   // LOW must persist this long = release (bridges dips)
    static const unsigned long minTapMs  = 30;    // ignore ultra-short blips
    unsigned long holdMs = 2000;                  // hold this long to switch page

    bool          held = false;        // debounced "finger down" state
    bool          longFired = false;   // LongPress already emitted for this press
    unsigned long pressStart = 0;      // when the current press began
    unsigned long lastHigh = 0;        // last time the raw signal was HIGH
};
