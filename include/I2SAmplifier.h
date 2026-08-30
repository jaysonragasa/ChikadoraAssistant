#pragma once
#include "IAudioOutput.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>

// Plays 16-bit PCM WAV audio on a MAX98357 over I2S, with two selectable
// strategies (see setStreamingMode):
//
//   Buffered (default): download the whole WAV into RAM, then clock it out from
//     memory. Immune to network timing so it's gap-free, but the clip must fit
//     in RAM - pair it with the reply being chopped into small clips.
//
//   Streaming: pull the WAV from HTTP and play it as it arrives, holding only a
//     small buffer. Handles any length, but real-time delivery can stutter on
//     the single-core C3 if the network hiccups.
class I2SAmplifier : public IAudioOutput {
private:
    int bclkPin;
    int lrcPin;
    int doutPin;

    bool i2sInstalled = false;
    bool streamMode = false;      // false = buffered, true = streaming

    // Format, parsed from the WAV header (either mode).
    uint32_t sampleRate = 16000;
    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;

    // ---- buffered (download-then-play) state ----
    std::vector<uint8_t> wavData;
    size_t playPos = 0;
    size_t dataEnd = 0;
    bool   bufPlaying = false;

    // ---- streaming state ----
    HTTPClient  http;
    WiFiClient* netStream = nullptr;
    long        pcmRemaining = 0;
    bool        streaming = false;

    // common
    void installI2S(uint32_t sr);
    void flushAndUninstall();

    // buffered
    bool downloadWav(const char* url);
    bool parseWavHeader(size_t& dataOffset, size_t& dataSize);
    void playUrlBuffered(const char* url);
    void updateBuffered();

    // streaming
    bool readExact(uint8_t* buf, int n, uint32_t timeoutMs);
    bool skipExact(int n, uint32_t timeoutMs);
    bool parseWavHeaderStreaming();
    void playUrlStreaming(const char* url);
    void updateStreaming();

public:
    I2SAmplifier(int bclk, int lrc, int dout);
    ~I2SAmplifier() override = default;

    void initialize() override;
    void setStreamingMode(bool enabled) override;
    void playUrl(const char* url) override;
    void playDingDong() override;
    void update() override;
    void stop() override;
    bool isPlaying() override;
    void debugAudio() override;
};
