#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// Chikadora configuration.
//
// All tunables live here so the rest of the firmware stays free of magic
// numbers and credentials. Values are compile-time constants, so the compiler
// folds them just like the old #defines did.
//
// Two kinds of settings live here:
//   * BOOTSTRAP (Wifi, Server::IP/PORT, Pins) - the device needs these before
//     it can reach the server, or they're physical wiring, so they can only be
//     changed by reflashing.
//   * DEFAULTS for the web-configurable runtime settings (Server::VOICE_ID,
//     Audio::*, Playback::MODE/WORDS_PER_CHUNK). These are fetched from
//     GET /api/device/settings at boot (see RuntimeSettings) and can be changed
//     from the web UI without reflashing; the values here are just the fallback
//     used when that fetch fails.
// -----------------------------------------------------------------------------
namespace Config {

// How the reply audio is played back on the device.
enum class PlaybackMode {
    Chunk,   // chop the reply into small clips, buffered download-then-play (smooth)
    Stream,  // send the whole reply as one job, stream it to I2S (any length)
};

namespace Wifi {
    constexpr const char* SSID = "highest_point";
    constexpr const char* PASS = "24f341%U";
}

// BOOTSTRAP: over-the-air update settings. HOSTNAME is how the device appears
// to the OTA uploader; set PASSWORD to require auth (empty = anyone on the LAN
// can flash it).
namespace Ota {
    constexpr const char* HOSTNAME = "chikadora";
    constexpr const char* PASSWORD = "";
}

namespace Server {
    // BOOTSTRAP: where to reach the phone server.
    constexpr const char* IP             = "192.168.1.34";
    constexpr uint16_t    PORT           = 8000;          // Legacy / fallback port
    constexpr uint16_t    KOKORO_PORT    = 8000;          // Kokoro TTS direct port
    constexpr uint16_t    MOONSHINE_PORT = 8002;          // Moonshine STT direct port
    constexpr uint16_t    OLLAMA_PORT    = 11434;         // Ollama LLM direct port

    // DEFAULT: web-overridable (device settings)
    constexpr const char* VOICE_ID       = "af_sarah";    // Kokoro voice
    constexpr const char* OLLAMA_MODEL   = "gemma4:31b-cloud";
    constexpr const char* SYSTEM_PROMPT  = "You are Mochi, a friendly and concise desk robot. Keep replies to 1-2 short sentences without markdown or emojis.";
}

namespace Personality {
    constexpr bool TARS_ENABLED    = false;
    constexpr int  TARS_HUMOR      = 75;
    constexpr int  TARS_HONESTY    = 90;
    constexpr int  TARS_SARCASM    = 30;
    constexpr int  TARS_DISCRETION = 90;
}

namespace Audio {
    // Mic gain as a clean multiplier over unity (1.0 = full 24-bit mic range
    // mapped to 16-bit). Raise if too quiet, lower if it clips. ~2-5 typical.
    constexpr float MIC_GAIN = 3.0f;

    // Speaker output volume: 0.0 silent .. 1.0 full scale. >1.0 amplifies/clips.
    constexpr float SPEAKER_VOLUME = 3.0f;

    // true  -> transcribed speech goes to the LLM (/api/chat) and the reply is spoken.
    // false -> the assistant just echoes what it heard.
    constexpr bool USE_LLM = true;

    // When nothing is transcribed: true = speak Messages::NO_SPEECH aloud;
    // false = stay silent (just the eye-shake).
    constexpr bool SPEAK_ON_NO_SPEECH = false;
}

// Canned spoken replies for failure cases (change the wording / language here).
namespace Messages {
    // Spoken when transcription comes back empty (nothing understood).
    constexpr const char* NO_SPEECH = "Sorry, I didn't catch that. Please try again.";
    // Spoken when the LLM can't be reached (server down, model missing, etc.).
    constexpr const char* LLM_ERROR = "Sorry, I couldn't reach my brain right now.";
}

namespace Agent {
    constexpr const char* NAME = "Mochi";
}

namespace Playback {
    constexpr PlaybackMode MODE = PlaybackMode::Stream;

    // Chunk mode: split the reply into clips of ~this many words; queue them all
    // up front so the server synthesizes the next while the current one plays.
    constexpr int WORDS_PER_CHUNK = 10;
    constexpr int MAX_CLIPS       = 24;  // upper bound on clips per reply
}

// Face/eye geometry defaults (web-overridable, with live preview). The face is
// drawn on a 128x64 OLED; eyes are centered horizontally around x=64.
namespace Eyes {
    constexpr int  WIDTH     = 30;   // eye width in px
    constexpr int  HEIGHT    = 34;   // eye height in px (normal/idle)
    constexpr int  GAP       = 27;   // distance from screen center to each eye center
    constexpr int  ROUNDNESS = 8;    // rounded-rect corner radius
    constexpr bool SHOW_MOUTH = true;
}

// Weather feature defaults (web-overridable). Uses Open-Meteo (no API key).
namespace Weather {
    constexpr bool  ENABLED      = true;
    constexpr float LAT          = 14.5995f;   // Manila (change on the config page)
    constexpr float LON          = 120.9842f;
    constexpr int   POLL_MINUTES = 30;         // how often to refresh the forecast
}

// Small top-left status overlays (drawn behind the eyes), web-overridable.
namespace Overlay {
    constexpr bool SHOW_LOOPTIME = false;   // main-loop frequency (Hz)
    constexpr bool SHOW_FREEMEM  = false;   // free heap (KB)
}

// Touch behavior (web-overridable). Tap = speak; holding the pad this long
// switches pages.
namespace Touch {
    constexpr int HOLD_MS = 2000;   // hold duration (ms) to switch page
}

// GPIO wiring. The mic and amp share BCLK/LRC (single I2S peripheral).
namespace Pins {
    constexpr int OLED_SDA = 20;
    constexpr int OLED_SCL = 21;
    constexpr int TOUCH    = 5;
    constexpr int I2S_BCLK = 2;
    constexpr int I2S_LRC  = 3;
    constexpr int AMP_DIN  = 4;   // MAX98357 data in
    constexpr int MIC_DOUT = 10;   // INMP441 data out
}

} // namespace Config
