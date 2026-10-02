#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <functional>
#include "IDisplay.h"

// A tiny web server the device hosts on port 80 so you can configure everything
// from a browser (at the device's own IP, shown on the boot screen) without
// reflashing. Values are saved to NVS via DeviceConfig and take effect after a
// restart - except the eye geometry, which is previewed live on the display.
//
// Routes:
//   GET  /         - config form, pre-filled with current settings
//   POST /save     - validate + persist, then show a "restart to apply" page
//   POST /restart  - reboot the device
//   POST /eye/live - apply eye geometry to the display immediately (no persist)
class WebConfigServer {
public:
    // `display` is used for the live eye preview. Call after Wi-Fi is up.
    void begin(IDisplay& display);
    void handle();   // pump the server; call from the main loop

    // Agent extension callbacks
    std::function<void()> onAgentStart;
    std::function<void()> onAgentThinking;
    std::function<void()> onAgentEnd;
    std::function<void(const String&)> onAgentSpeak;

private:
    WebServer server{80};
    IDisplay* display = nullptr;

    void handleRoot();
    void handleSave();
    void handleRestart();
    void handleEyeLive();
    
    // Agent endpoints
    void handleAgentStart();
    void handleAgentThinking();
    void handleAgentEnd();
    void handleAgentSpeak();
};
