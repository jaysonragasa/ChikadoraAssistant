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

    // What the screen is currently showing. Boot screens (Connecting/Connected/
    // Sleeping) take over the display until Wi-Fi is up; then it's the Face.
    enum class Screen { Face, Connecting, Connected, Sleeping };
    Screen screen = Screen::Face;

    // Idle ambient animation: eyes drift ("wander") and blink on their own.
    int gazeX = 0;                 // current eye offset from center
    int gazeY = 0;
    unsigned long nextBlinkAt = 0; // when to auto-blink next (idle only)
    unsigned long nextWanderAt = 0;// when to shift gaze next (idle only)

    // Boot-screen state: what to print, plus a shared spinner/animation clock.
    String        bootSsid;
    String        bootIp;
    int           spinnerFrame = 0;
    unsigned long nextSpinnerAt = 0;

    void drawFace(Expression expr, bool blinkClosed);
    void scheduleIdleAnim();       // (re)arm the idle blink/wander timers

    // Boot-screen renderers.
    void drawSpinner(int cx, int cy, int r, int frame);
    void drawConnecting();
    void drawConnected();
    void drawSleeping();

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
    void showConnecting(const char* ssid) override;
    void showConnected(const char* ip) override;
    void showSleeping() override;
    void update() override;
};
