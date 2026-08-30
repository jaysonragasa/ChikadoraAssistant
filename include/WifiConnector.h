#pragma once

// Isolates Wi-Fi bring-up so the rest of the app doesn't touch the WiFi API.
namespace WifiConnector {

// Connect to the given network (station mode). Blocks until connected. Also
// lowers TX power to avoid brownouts on breadboard supplies and logs a scan.
void connect(const char* ssid, const char* pass);

} // namespace WifiConnector
