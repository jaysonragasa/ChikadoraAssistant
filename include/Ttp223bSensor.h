#pragma once

#include "ITouchSensor.h"

class Ttp223bSensor : public ITouchSensor {
private:
    int pin;
public:
    Ttp223bSensor(int pin);
    ~Ttp223bSensor() override = default;

    void initialize() override;
    bool isTouched() override;
};
