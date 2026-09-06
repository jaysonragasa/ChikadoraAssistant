#pragma once

// Over-the-air firmware updates (ArduinoOTA). Lets you flash the device over
// Wi-Fi instead of USB. Isolated here so the rest of the app doesn't touch the
// OTA API. Requires an OTA-capable partition table (the default 4MB scheme has
// two app slots, which this project already uses).
namespace OtaUpdater {

// Start the OTA listener. Call once after Wi-Fi is connected. If `password` is
// non-empty, uploads must authenticate with it.
void begin(const char* hostname, const char* password);

// Pump the OTA handler; call from the main loop.
void handle();

} // namespace OtaUpdater
