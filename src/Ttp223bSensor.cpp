#include "Ttp223bSensor.h"
#include <Arduino.h>

Ttp223bSensor::Ttp223bSensor(int pin) : pin(pin) {}

void Ttp223bSensor::initialize() {
    // The TTP223B outputs HIGH when touched
    pinMode(pin, INPUT);
}

bool Ttp223bSensor::isTouched() {
    return digitalRead(pin) == HIGH;
}
