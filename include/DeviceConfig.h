#pragma once
#include <Arduino.h>
#include "RuntimeSettings.h"

// Persistent, on-device configuration stored in NVS (flash) via Preferences.
//
// This is the single source of truth for everything tunable at runtime: the
// TTS server address plus the audio/playback settings. It's edited from the
// on-device web config page (no reflashing, and no dependency on the server
// being reachable). Config.h supplies the first-run defaults; once the user
// saves, the stored values win.
namespace DeviceConfig {

// Open the NVS namespace. Call once early in setup (safe before Wi-Fi).
void begin();

// --- TTS server address (bootstrap; needed before we can reach the server) ---
String   serverIp();
uint16_t serverPort();
void     setServer(const String& ip, uint16_t port);

// --- Runtime audio/playback settings ---
// Returns all settings, using the stored value where present and the Config.h
// default otherwise.
RuntimeSettings settings();
// Persist all runtime settings.
void setSettings(const RuntimeSettings& s);

} // namespace DeviceConfig
