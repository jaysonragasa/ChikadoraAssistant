# Chikadora firmware — project guide

ESP32-C3 Supermini voice assistant (codename Chikadora). Tap-to-talk: mic →
local server (Whisper STT → Ollama → Kokoro TTS) → speaker, with an OLED face.
A companion Python webserver lives in `webserver/`.

## Build / flash (PlatformIO, Windows PowerShell)

- Build: `& "C:\Users\aragasa\.platformio\penv\Scripts\pio.exe" run -e esp32-c3-supermini`
  (cwd = project root `c:\Vibe\Mochi\ESP32C3\Mochi`). Builds are slow (~30–210 s); use a long timeout.
- Flash: add `-t upload`. Monitor: `-t monitor` (115200 baud). Helper scripts:
  `upload.cmd`, `uploadmon.cmd`.
- **Always build after firmware changes** before claiming success.
- Do NOT run long-running/watch commands.

## Architecture (SOLID — keep it this way)

- `main.cpp` is a **composition root**: it constructs concrete devices and
  injects them into `VoiceAssistant` via constructor references.
- `VoiceAssistant` depends only on interfaces: `IDisplay`, `IAudioInput`,
  `IAudioOutput`, `ITouchSensor`, `IApiClient`, `IWeatherProvider`.
- Concrete impls: `OledFaceDisplay`, `I2SMicrophone`, `I2SAmplifier`,
  `Ttp223bSensor`, `LocalTtsClient`, `OpenMeteoClient`.
- Pure helpers (header-only): `TextChunker` (reply → clips), `TapDetector`
  (single/double tap). Namespaced utilities: `WifiConnector`, `DeviceConfig`.
- When adding a capability, prefer a new interface + concrete impl injected at
  the composition root over growing a god-class.

## Configuration model (important)

- `include/Config.h` = compile-time **first-run defaults** (namespaced constexpr).
- `RuntimeSettings` = mutable struct seeded from Config defaults.
- `DeviceConfig` = NVS (flash) persistence; `settings()` loads, `setSettings()`
  saves. **On the device, NVS is the single source of truth.**
- `WebConfigServer` (port 80, hosted by the device) is where the user edits
  everything: server IP/port, voice, mic gain, volume, USE_LLM, no-speech,
  playback mode, words/chunk, eye geometry (live preview via `/eye/live`),
  overlays, weather (lat/lon/interval/enable). Most settings apply on restart.
- **Bootstrap/hardware settings stay compile-time**: Wi-Fi credentials and
  `Config::Pins` — the device needs Wi-Fi before it can serve its config page,
  and pins are physical wiring.
- The Python webserver does NOT hold device settings anymore (retired); it only
  covers TTS generation + Ollama. Don't reintroduce a server-side device-settings
  fetch.

## Pins (`Config::Pins`)

OLED_SDA=20, OLED_SCL=21, TOUCH=5, I2S_BCLK=2, I2S_LRC=3, AMP_DIN=4 (MAX98357),
MIC_DOUT=10 (INMP441). Mic + amp share BCLK/LRC (single I2S peripheral, installed
on demand). GPIO5 touch has been historically finicky; touch uses plain `INPUT`.

## Display notes

- One `Adafruit_SSD1306`; a `Screen` enum picks what's shown (Face, boot screens,
  Weather). Face `drawFace` draws **mouth first, then eyes** so the smile mask
  doesn't bite the eyes; overlays/`!TTS` badge are drawn **behind** the eyes.
- Eye geometry is parameterized (width/height/gap/roundness/showMouth) and set via
  `setEyeConfig` (used for live preview). Idle gaze eases smoothly and the mouth
  drifts with it.

## Conventions

- Match the existing terse comment style explaining *why*.
- Firmware runs on a tiny MCU: watch heap; free the mic buffer before playback;
  keep JSON docs bounded (use ArduinoJson filters + capped requests).
- Git: repo is **public** (github.com/jaysonragasa/ChikadoraAssistant), default
  branch `main`. `include/Config.h` currently contains real Wi-Fi credentials —
  **keep it out of commits/pushes** unless the user explicitly asks. Commit only
  when asked. PowerShell shows git stderr in red even on success; verify with
  `git status -sb`.
