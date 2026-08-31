#pragma once
#include <Arduino.h>
#include "ITrigger.h"
#include "IAudioInput.h"
#include "ITouchSensor.h"

// Sound-activated start: while idle it monitors the mic level and triggers when
// the input stays above `threshold` for `minMs` (debounced so brief pops don't
// fire). A tap still works as a fallback. This is "noise activated" - it reacts
// to any loud-enough sound, not a specific wake word.
class VoiceTrigger : public ITrigger {
public:
    VoiceTrigger(IAudioInput& mic, ITouchSensor& touch, int threshold,
                 unsigned long minMs, bool debug = false, bool touchFallback = true)
        : mic(mic), touch(touch), threshold(threshold), minMs(minMs),
          debug(debug), touchFallback(touchFallback) {}

    void begin() override { touch.initialize(); }

    void onEnterIdle() override {
        loudStartMs = 0;
        mic.startMonitoring();   // install mic I2S for level sampling
    }

    void onExitIdle() override {
        mic.stopMonitoring();    // free the I2S port for feedback tone / recording
    }

    bool triggered() override {
        // Touch fallback (rising edge) - optional.
        if (touchFallback) {
            bool tNow = touch.isTouched();
            bool tEdge = tNow && !wasTouched;
            wasTouched = tNow;
            if (tEdge) {
                Serial.println("[VoiceTrigger] start: TOUCH");
                return true;
            }
        }

        // Sound onset with a sustain debounce.
        int level = mic.readPeakLevel();
        unsigned long now = millis();

        if (debug && now - lastDebugMs >= 400) {
            lastDebugMs = now;
            Serial.printf("[VoiceTrigger] level=%d (start>=%d)\n", level, threshold);
        }

        if (level >= threshold) {
            if (loudStartMs == 0) loudStartMs = now;
            if (now - loudStartMs >= minMs) {
                loudStartMs = 0;
                Serial.printf("[VoiceTrigger] start: SOUND level=%d\n", level);
                return true;
            }
        } else {
            loudStartMs = 0;
        }
        return false;
    }

private:
    IAudioInput&  mic;
    ITouchSensor& touch;
    int           threshold;
    unsigned long minMs;
    bool          debug;
    bool          touchFallback;
    unsigned long loudStartMs = 0;
    unsigned long lastDebugMs = 0;
    bool          wasTouched = false;
};
