#include "Ttp223bSensor.h"
#include <Arduino.h>

Ttp223bSensor::Ttp223bSensor(int pin) : pin(pin) {}

void Ttp223bSensor::initialize() {
    // TTP223B drives HIGH when touched. Use a pulldown so a noisy/floating line
    // rests LOW rather than picking up spurious highs (the sensor overpowers it).
    pinMode(pin, INPUT_PULLDOWN);
}

bool Ttp223bSensor::isTouched() {
    bool raw = digitalRead(pin) == HIGH;
    unsigned long now = millis();
    if (raw != lastRaw) {          // level changed -> restart the stability timer
        lastRaw = raw;
        stableSince = now;
    }
    // Only report a touch once the pad has been HIGH steadily past the debounce.
    return raw && (now - stableSince >= debounceMs);
}
