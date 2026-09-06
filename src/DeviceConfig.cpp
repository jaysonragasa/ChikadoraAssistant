#include "DeviceConfig.h"
#include "Config.h"
#include <Preferences.h>

namespace DeviceConfig {

static Preferences prefs;

// NVS keys are limited to 15 chars; keep them short.
static const char* NS         = "chikadora";
static const char* KEY_IP     = "srv_ip";
static const char* KEY_PORT   = "srv_port";
static const char* KEY_VOICE  = "voice";
static const char* KEY_MICG   = "mic_gain";
static const char* KEY_VOL    = "spk_vol";
static const char* KEY_LLM    = "use_llm";
static const char* KEY_NOSPCH = "no_speech";
static const char* KEY_PBMODE = "pb_mode";     // "stream" | "chunk"
static const char* KEY_WPC    = "wpc";
// Eyes
static const char* KEY_EYE_W  = "eye_w";
static const char* KEY_EYE_H  = "eye_h";
static const char* KEY_EYE_GAP= "eye_gap";
static const char* KEY_EYE_R  = "eye_r";
static const char* KEY_MOUTH  = "show_mouth";
// Weather
static const char* KEY_WX_EN  = "wx_en";
static const char* KEY_WX_LAT = "wx_lat";
static const char* KEY_WX_LON = "wx_lon";
static const char* KEY_WX_MIN = "wx_min";
// Overlays
static const char* KEY_OV_LOOP= "ov_loop";
static const char* KEY_OV_MEM = "ov_mem";

static const char* modeToStr(Config::PlaybackMode m) {
    return m == Config::PlaybackMode::Stream ? "stream" : "chunk";
}

void begin() {
    prefs.begin(NS, false);   // read/write
}

// --- server address ---
String serverIp() {
    return prefs.getString(KEY_IP, Config::Server::IP);
}

uint16_t serverPort() {
    return prefs.getUShort(KEY_PORT, Config::Server::PORT);
}

void setServer(const String& ip, uint16_t port) {
    prefs.putString(KEY_IP, ip);
    prefs.putUShort(KEY_PORT, port);
    Serial.printf("[Config] Saved TTS server: %s:%u (restart to apply)\n", ip.c_str(), port);
}

// --- runtime settings ---
RuntimeSettings settings() {
    RuntimeSettings s;   // members already hold the Config.h defaults
    s.voiceId         = prefs.getString(KEY_VOICE, s.voiceId);
    s.micGain         = prefs.getFloat(KEY_MICG, s.micGain);
    s.speakerVolume   = prefs.getFloat(KEY_VOL, s.speakerVolume);
    s.useLlm          = prefs.getBool(KEY_LLM, s.useLlm);
    s.speakOnNoSpeech = prefs.getBool(KEY_NOSPCH, s.speakOnNoSpeech);
    s.wordsPerChunk   = prefs.getInt(KEY_WPC, s.wordsPerChunk);
    String mode       = prefs.getString(KEY_PBMODE, modeToStr(s.playbackMode));
    s.playbackMode    = (mode == "chunk") ? Config::PlaybackMode::Chunk
                                          : Config::PlaybackMode::Stream;
    // Eyes
    s.eyeWidth        = prefs.getInt(KEY_EYE_W, s.eyeWidth);
    s.eyeHeight       = prefs.getInt(KEY_EYE_H, s.eyeHeight);
    s.eyeGap          = prefs.getInt(KEY_EYE_GAP, s.eyeGap);
    s.eyeRoundness    = prefs.getInt(KEY_EYE_R, s.eyeRoundness);
    s.showMouth       = prefs.getBool(KEY_MOUTH, s.showMouth);
    // Weather
    s.weatherEnabled     = prefs.getBool(KEY_WX_EN, s.weatherEnabled);
    s.weatherLat         = prefs.getFloat(KEY_WX_LAT, s.weatherLat);
    s.weatherLon         = prefs.getFloat(KEY_WX_LON, s.weatherLon);
    s.weatherPollMinutes = prefs.getInt(KEY_WX_MIN, s.weatherPollMinutes);
    // Overlays
    s.showLoopTime    = prefs.getBool(KEY_OV_LOOP, s.showLoopTime);
    s.showFreeMem     = prefs.getBool(KEY_OV_MEM, s.showFreeMem);
    return s;
}

void setSettings(const RuntimeSettings& s) {
    prefs.putString(KEY_VOICE, s.voiceId);
    prefs.putFloat(KEY_MICG, s.micGain);
    prefs.putFloat(KEY_VOL, s.speakerVolume);
    prefs.putBool(KEY_LLM, s.useLlm);
    prefs.putBool(KEY_NOSPCH, s.speakOnNoSpeech);
    prefs.putInt(KEY_WPC, s.wordsPerChunk);
    prefs.putString(KEY_PBMODE, modeToStr(s.playbackMode));
    // Eyes
    prefs.putInt(KEY_EYE_W, s.eyeWidth);
    prefs.putInt(KEY_EYE_H, s.eyeHeight);
    prefs.putInt(KEY_EYE_GAP, s.eyeGap);
    prefs.putInt(KEY_EYE_R, s.eyeRoundness);
    prefs.putBool(KEY_MOUTH, s.showMouth);
    // Weather
    prefs.putBool(KEY_WX_EN, s.weatherEnabled);
    prefs.putFloat(KEY_WX_LAT, s.weatherLat);
    prefs.putFloat(KEY_WX_LON, s.weatherLon);
    prefs.putInt(KEY_WX_MIN, s.weatherPollMinutes);
    // Overlays
    prefs.putBool(KEY_OV_LOOP, s.showLoopTime);
    prefs.putBool(KEY_OV_MEM, s.showFreeMem);
    Serial.println("[Config] Saved device settings (restart to apply)");
}

} // namespace DeviceConfig
