#include "Ttp223bSensor.h"
#include <Arduino.h>

Ttp223bSensor::Ttp223bSensor(int pin) : pin(pin) {}

void Ttp223bSensor::initialize() {
    // Plain INPUT, exactly like the working main branch. (INPUT_PULLDOWN held
    // the line low and broke touch on this hardware - the TTP223B output on
    // this board doesn't overpower the internal pulldown.)
    pinMode(pin, INPUT);
}

bool Ttp223bSensor::isTouched() {
    bool raw = digitalRead(pin) == HIGH;
    if (raw != lastRaw) {   // DEBUG: confirm the pin reacts to touch
        lastRaw = raw;
        Serial.printf("[Touch] GPIO%d -> %s\n", pin, raw ? "HIGH" : "LOW");
    }
    return raw;   // direct read, no debounce (matches main)
}
