#pragma once

#include "IDisplay.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

class OledFaceDisplay : public IDisplay {
private:
    Adafruit_SSD1306 display;
    int sdaPin;
    int sclPin;

    // Animation state
    unsigned long lastUpdate;
    bool isBlinking;
    unsigned long blinkStartTime;
    const unsigned long blinkDuration = 150;

    void drawFace(bool eyesClosed);

public:
    OledFaceDisplay(int sda, int scl);
    ~OledFaceDisplay() override = default;

    void initialize() override;
    void showIdle() override;
    void showListening() override;
    void showSpeaking() override;
    void triggerBlink() override;
    void update() override;
};
