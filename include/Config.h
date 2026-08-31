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

// How a conversation is started.
enum class TriggerMode {
    Touch,   // tap the TTP223B pad
    Voice,   // sound-activated: start when it hears you (touch still works as fallback)
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
    constexpr PlaybackMode MODE = PlaybackMode::Stream;

    // Chunk mode: split the reply into clips of ~this many words; queue them all
    // up front so the server synthesizes the next while the current one plays.
    constexpr int WORDS_PER_CHUNK = 10;
    constexpr int MAX_CLIPS       = 24;  // upper bound on clips per reply
}

namespace Trigger {
    constexpr TriggerMode MODE = TriggerMode::Voice;  // Touch or Voice

    // --- Voice activation (sound onset). Levels are 16-bit peak amplitude
    //     (0..32767) after MIC_GAIN. Tune to your room/mic distance. ---
    // Start recording when input peak stays above this for START_MIN_MS.
    constexpr int           START_THRESHOLD = 3000;
    constexpr unsigned long START_MIN_MS    = 120;   // debounce (ignore short pops)

    // --- End-of-speech (silence) detection while recording ---
    constexpr int           SILENCE_THRESHOLD = 1400; // below this counts as silence
    constexpr unsigned long SILENCE_MS        = 900;   // trailing silence -> stop
    constexpr unsigned long MIN_SPEECH_MS     = 400;   // always record at least this long
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
