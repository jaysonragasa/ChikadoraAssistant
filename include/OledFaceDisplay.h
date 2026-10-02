#pragma once

#include "IDisplay.h"
#include "WeatherData.h"
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
    enum class Expression { Normal, Listening, Thinking, Happy, AgentThinking, AgentSpeaking };
    Expression currentExpr = Expression::Normal;

    // What the screen is currently showing. Boot screens take over the display
    // until bring-up finishes; then it's the Face.
    enum class Screen { Face, Connecting, Connected, ConnectingServer, Sleeping, Weather };
    Screen screen = Screen::Face;

    // Idle ambient animation: eyes (and mouth) drift ("wander") and blink on
    // their own. The gaze eases smoothly toward a target rather than snapping.
    float gazeX = 0;               // current offset from center (eased, float)
    float gazeY = 0;
    int   targetGazeX = 0;         // where the gaze is gliding toward
    int   targetGazeY = 0;
    unsigned long nextBlinkAt = 0; // when to auto-blink next (idle only)
    unsigned long nextWanderAt = 0;// when to pick a new gaze target (idle only)
    unsigned long nextFrameAt = 0; // throttles the easing redraw (~30 fps)

    // Shown as badges on the face when the server components are unreachable.
    ServerStatus  srvStatus;

    // Configurable eye geometry (live-previewable / persisted). Eyes are centered
    // horizontally around x=64; each eye sits `eyeGap` px from center.
    int  eyeWidth     = 30;
    int  eyeHeight    = 34;
    int  eyeGap       = 27;
    int  eyeRoundness = 8;
    bool showMouth    = true;

    // Top-left status overlays (drawn behind the eyes).
    bool     showLoopTime = false;
    bool     showFreeMem  = false;
    float    loopHz       = 0;
    uint32_t freeHeap     = 0;

    // Boot-screen state: what to print, plus a shared spinner/animation clock.
    String        bootSsid;
    String        bootIp;
    String        bootHost;
    int           spinnerFrame = 0;
    unsigned long nextSpinnerAt = 0;

    // Weather page state.
    WeatherData   weatherData;
    int           weatherFrame = 0;      // drives icon animation
    unsigned long nextWeatherFrameAt = 0;

    void drawFace(Expression expr, bool blinkClosed);
    void scheduleIdleAnim();       // (re)arm the idle blink/wander timers

    // Boot-screen renderers.
    void drawSpinner(int cx, int cy, int r, int frame);
    void drawConnecting();
    void drawConnected();
    void drawConnectingServer();
    void drawSleeping();
    void drawWeather();
    void drawWeatherIcon(int cx, int cy, int size, WeatherCategory cat, int frame);

public:
    OledFaceDisplay(int sda, int scl);
    ~OledFaceDisplay() override = default;

    void initialize() override;
    void showIdle() override;
    void showListening() override;
    void showThinking() override;
    void showSpeaking() override;
    void showAgentThinking() override;
    void showAgentSpeaking() override;
    void triggerBlink() override;
    void shakeEyes() override;
    void showConnecting(const char* ssid) override;
    void showConnected(const char* ip) override;
    void showConnectingServer(const char* host) override;
    void showWeather() override;
    void setWeather(const WeatherData& data) override;
    void showSleeping() override;
    void setServerStatus(const ServerStatus& status) override;
    void setEyeConfig(int width, int height, int gap, int roundness, bool showMouth) override;
    void setOverlayFlags(bool showLoopTime, bool showFreeMem) override;
    void setMetrics(float loopHz, uint32_t freeHeapBytes) override;
    void update() override;
};
