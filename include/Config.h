#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// Chikadora configuration.
//
// All tunables live here so the rest of the firmware stays free of magic
// numbers and credentials. Values are compile-time constants, so the compiler
// folds them just like the old #defines did.
// -----------------------------------------------------------------------------
namespace Config {

// How the reply audio is played back on the device.
enum class PlaybackMode {
    Chunk,   // chop the reply into small clips, buffered download-then-play (smooth)
    Stream,  // send the whole reply as one job, stream it to I2S (any length)
};

namespace Wifi {
    constexpr const char* SSID = "Anikanik2G";
    constexpr const char* PASS = "QazWsx12345";
}

namespace Server {
    constexpr const char* IP       = "192.168.1.60";  // PC running the local server
    constexpr uint16_t    PORT     = 8000;
    constexpr const char* VOICE_ID = "af_sarah";      // Kokoro voice
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
}

namespace Playback {
    constexpr PlaybackMode MODE = PlaybackMode::Chunk;

    // Chunk mode: split the reply into clips of ~this many words; queue them all
    // up front so the server synthesizes the next while the current one plays.
    constexpr int WORDS_PER_CHUNK = 10;
    constexpr int MAX_CLIPS       = 24;  // upper bound on clips per reply
}

// GPIO wiring. The mic and amp share BCLK/LRC (single I2S peripheral).
namespace Pins {
    constexpr int OLED_SDA = 9;
    constexpr int OLED_SCL = 8;
    constexpr int TOUCH    = 10;
    constexpr int I2S_BCLK = 2;
    constexpr int I2S_LRC  = 3;
    constexpr int AMP_DIN  = 4;   // MAX98357 data in
    constexpr int MIC_DOUT = 5;   // INMP441 data out
}

} // namespace Config
