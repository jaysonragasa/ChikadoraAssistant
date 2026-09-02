#pragma once

#include <Arduino.h>

// Isolates Wi-Fi bring-up so the rest of the app doesn't touch the WiFi API.
namespace WifiConnector {

// Start connecting to the given network (station mode). Non-blocking: sets up
// station mode, lowers TX power to avoid brownouts on breadboard supplies, logs
// a scan, and kicks off WiFi.begin(). Poll isConnected() to know when it's up.
void begin(const char* ssid, const char* pass);

// Re-issue the connection attempt (used to nudge a stalled retry).
void retry(const char* ssid, const char* pass);

// True once the station has an IP / association.
bool isConnected();

// The device's IP address as a string (valid once isConnected() is true).
String ip();

} // namespace WifiConnector
