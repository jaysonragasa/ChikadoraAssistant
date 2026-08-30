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

    // Facial expression per assistant state. Tracked so a blink can restore
    // whatever expression is currently showing.
    enum class Expression { Normal, Listening, Thinking, Happy };
    Expression currentExpr = Expression::Normal;

    // Idle ambient animation: eyes drift ("wander") and blink on their own.
    int gazeX = 0;                 // current eye offset from center
    int gazeY = 0;
    unsigned long nextBlinkAt = 0; // when to auto-blink next (idle only)
    unsigned long nextWanderAt = 0;// when to shift gaze next (idle only)

    void drawFace(Expression expr, bool blinkClosed);
    void scheduleIdleAnim();       // (re)arm the idle blink/wander timers

public:
    OledFaceDisplay(int sda, int scl);
    ~OledFaceDisplay() override = default;

    void initialize() override;
    void showIdle() override;
    void showListening() override;
    void showThinking() override;
    void showSpeaking() override;
    void triggerBlink() override;
    void shakeEyes() override;
    void update() override;
};
