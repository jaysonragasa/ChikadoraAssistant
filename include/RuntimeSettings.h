#pragma once
#include <Arduino.h>
#include "Config.h"

// Mutable, runtime copy of the web-configurable settings. Seeded from the
// compile-time Config defaults, then overwritten by values fetched from the
// server at boot (GET /api/device/settings). If the fetch fails, these defaults
// are used unchanged.
//
// Bootstrap settings (Wi-Fi credentials, server IP/port) and hardware pins are
// deliberately NOT here: the device needs them before it can reach the server,
// so they stay compile-time in Config.h.
struct RuntimeSettings {
    String voiceId                    = Config::Server::VOICE_ID;
    float  micGain                    = Config::Audio::MIC_GAIN;
    float  speakerVolume              = Config::Audio::SPEAKER_VOLUME;
    bool   useLlm                     = Config::Audio::USE_LLM;
    bool   speakOnNoSpeech            = Config::Audio::SPEAK_ON_NO_SPEECH;
    Config::PlaybackMode playbackMode = Config::Playback::MODE;
    int    wordsPerChunk              = Config::Playback::WORDS_PER_CHUNK;
    String agentName                  = Config::Agent::NAME;

    // Face / eye geometry (live-previewable from the web config page).
    int    eyeWidth                   = Config::Eyes::WIDTH;
    int    eyeHeight                  = Config::Eyes::HEIGHT;
    int    eyeGap                     = Config::Eyes::GAP;
    int    eyeRoundness               = Config::Eyes::ROUNDNESS;
    bool   showMouth                  = Config::Eyes::SHOW_MOUTH;

    // Weather (Open-Meteo).
    bool   weatherEnabled             = Config::Weather::ENABLED;
    float  weatherLat                 = Config::Weather::LAT;
    float  weatherLon                 = Config::Weather::LON;
    int    weatherPollMinutes         = Config::Weather::POLL_MINUTES;

    // Top-left status overlays (drawn behind the eyes).
    bool   showLoopTime               = Config::Overlay::SHOW_LOOPTIME;
    bool   showFreeMem                = Config::Overlay::SHOW_FREEMEM;

    // Touch: hold this many ms to switch pages (tap = speak).
    int    holdMs                     = Config::Touch::HOLD_MS;

    // Multi-port AI server endpoints on phone
    uint16_t kokoroPort               = Config::Server::KOKORO_PORT;
    uint16_t moonshinePort            = Config::Server::MOONSHINE_PORT;
    uint16_t ollamaPort               = Config::Server::OLLAMA_PORT;

    // Ollama model & system prompt
    String ollamaModel                = Config::Server::OLLAMA_MODEL;
    String ollamaSystemPrompt         = Config::Server::SYSTEM_PROMPT;

    // TARS personality tuning
    bool   tarsEnabled                = Config::Personality::TARS_ENABLED;
    int    tarsHumor                  = Config::Personality::TARS_HUMOR;
    int    tarsHonesty                = Config::Personality::TARS_HONESTY;
    int    tarsSarcasm                = Config::Personality::TARS_SARCASM;
    int    tarsDiscretion             = Config::Personality::TARS_DISCRETION;
};
