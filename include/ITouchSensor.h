#pragma once

class ITouchSensor {
public:
    virtual ~ITouchSensor() = default;
    virtual void initialize() = 0;
    virtual bool isTouched() = 0;
};
