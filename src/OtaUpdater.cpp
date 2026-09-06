#include "OtaUpdater.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <string.h>

namespace OtaUpdater {

void begin(const char* hostname, const char* password) {
    if (hostname && strlen(hostname) > 0) ArduinoOTA.setHostname(hostname);
    const bool hasPass = password && strlen(password) > 0;
    if (hasPass) ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([]() { Serial.println("\n[OTA] Update starting..."); });
    ArduinoOTA.onEnd([]()   { Serial.println("\n[OTA] Done. Rebooting."); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("[OTA] %u%%\r", t ? (p * 100u) / t : 0u);
    });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error %u\n", e); });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Ready as '%s'%s\n", hostname,
                  hasPass ? " (password protected)"
                          : " (NO PASSWORD - anyone on the LAN can flash it)");
}

void handle() {
    ArduinoOTA.handle();
}

} // namespace OtaUpdater
