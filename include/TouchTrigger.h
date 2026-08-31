#pragma once
#include "ITrigger.h"
#include "ITouchSensor.h"

// Starts a conversation on a rising-edge tap of the touch pad.
class TouchTrigger : public ITrigger {
public:
    explicit TouchTrigger(ITouchSensor& touch) : touch(touch) {}

    void begin() override { touch.initialize(); }

    bool triggered() override {
        bool now = touch.isTouched();
        bool edge = now && !wasTouched;   // rising edge only
        wasTouched = now;
        return edge;
    }

private:
    ITouchSensor& touch;
    bool wasTouched = false;
};
