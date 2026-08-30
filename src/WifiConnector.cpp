#include "WifiConnector.h"
#include <WiFi.h>

namespace WifiConnector {

void connect(const char* ssid, const char* pass) {
    Serial.print("Connecting to WiFi");

    // Station mode, clear any stale config.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Lower TX power to prevent voltage drops on the breadboard.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Log nearby networks (handy for diagnosing a bad antenna / wrong SSID).
    Serial.println("\n--- WiFi Scan ---");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("No networks found! (Is the antenna damaged?)");
    } else {
        Serial.printf("%d networks found:\n", n);
        for (int i = 0; i < n; ++i) {
            Serial.printf("%d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    Serial.println("-----------------");

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            Serial.print("\n[WiFi] Disconnected! Reason: ");
            Serial.println(info.wifi_sta_disconnected.reason);
        }
    });

    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
}

} // namespace WifiConnector
