#pragma once

#include "ITouchSensor.h"

class Ttp223bSensor : public ITouchSensor {
private:
    int pin;
    // Debounce: require the pad to read HIGH continuously for this long before
    // reporting a touch, so electrical noise spikes don't register as taps.
    static const unsigned long debounceMs = 40;
    bool          lastRaw = false;
    unsigned long stableSince = 0;
public:
    Ttp223bSensor(int pin);
    ~Ttp223bSensor() override = default;

    void initialize() override;
    bool isTouched() override;
};
