#include "I2SAmplifier.h"
#include <driver/i2s.h>

// Buffered mode: stereo frames pushed to I2S per update() call.
#define PLAYBACK_FRAMES_PER_UPDATE 256
// Streaming mode: bytes read from the network per update() call (~21 ms @16k).
#define READ_CHUNK 1024

I2SAmplifier::I2SAmplifier(int bclk, int lrc, int dout)
    : bclkPin(bclk), lrcPin(lrc), doutPin(dout) {}

void I2SAmplifier::initialize() {
    // I2S is installed on demand (playUrl / playDingDong) since the mic shares
    // the single I2S_NUM_0 peripheral.
}

void I2SAmplifier::setStreamingMode(bool enabled) {
    streamMode = enabled;
    Serial.printf("[Audio] Playback mode: %s\n", enabled ? "STREAMING" : "BUFFERED");
}

void I2SAmplifier::setVolume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 4.0f) v = 4.0f;   // allow some boost, but keep it sane
    volume = v;
    Serial.printf("[Audio] Volume: %.2f\n", volume);
}

// Apply volume with clamping. Skips the math entirely at unity for efficiency.
inline int16_t I2SAmplifier::scaleSample(int16_t s) {
    if (volume == 1.0f) return s;
    int32_t v = (int32_t)(s * volume);
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    return (int16_t)v;
}

// ---------------------------------------------------------------------------
// Common: I2S driver install / teardown.
// Streaming needs a big DMA cushion to ride out network jitter; buffered feeds
// from RAM so it uses small buffers for a short tail flush (tight clip gaps).
// ---------------------------------------------------------------------------
void I2SAmplifier::installI2S(uint32_t sr) {
    if (i2sInstalled) {
        i2s_driver_uninstall(I2S_NUM_0);
        i2sInstalled = false;
    }

    int bufCount = streamMode ? 16 : 8;
    int bufLen   = streamMode ? 512 : 256;

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = sr,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = bufCount,
        .dma_buf_len = bufLen,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num = bclkPin,
        .ws_io_num = lrcPin,
        .data_out_num = doutPin,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2sInstalled = true;
}

void I2SAmplifier::flushAndUninstall() {
    if (!i2sInstalled) return;
    // Let the DMA clock out its tail before teardown (sized to the DMA depth).
    vTaskDelay(pdMS_TO_TICKS(streamMode ? 400 : 150));
    i2s_driver_uninstall(I2S_NUM_0);
    i2sInstalled = false;
}

// ===========================================================================
// BUFFERED MODE (download-then-play)
// ===========================================================================
bool I2SAmplifier::downloadWav(const char* url) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Audio] WiFi not connected");
        return false;
    }

    HTTPClient dl;
    if (!dl.begin(url)) {
        Serial.println("[Audio] http.begin failed");
        return false;
    }
    int code = dl.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Audio] HTTP GET returned %d\n", code);
        dl.end();
        return false;
    }

    int len = dl.getSize();
    WiFiClient* s = dl.getStreamPtr();

    // Never let reserve() throw std::bad_alloc (the C3's unwinder crashes on a
    // throw). Require a known, capped length that fits a contiguous block.
    const size_t MAX_WAV = 250 * 1024;
    const size_t MARGIN  = 24 * 1024;
    if (len <= 0) {
        Serial.println("[Audio] No Content-Length; refusing unbounded download");
        dl.end();
        return false;
    }
    if ((size_t)len > MAX_WAV) {
        Serial.printf("[Audio] WAV too large (%d B); skipping\n", len);
        dl.end();
        return false;
    }
    size_t largest = ESP.getMaxAllocHeap();
    if ((size_t)len + MARGIN > largest) {
        Serial.printf("[Audio] Not enough contiguous heap (need %d B, largest %u B); "
                      "lower TTS_OUTPUT_RATE or use smaller clips. Skipping.\n",
                      len, (unsigned)largest);
        dl.end();
        return false;
    }

    wavData.clear();
    wavData.reserve(len);

    uint8_t buf[1024];
    unsigned long lastData = millis();
    while (dl.connected() && (int)wavData.size() < len) {
        size_t avail = s->available();
        if (avail) {
            int toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
            if ((int)(wavData.size() + toRead) > len) toRead = len - wavData.size();
            int r = s->readBytes(buf, toRead);
            if (r > 0) {
                wavData.insert(wavData.end(), buf, buf + r);
                lastData = millis();
            }
        } else {
            if (millis() - lastData > 5000) {
                Serial.println("[Audio] Download stalled, aborting");
                break;
            }
            delay(1);
        }
    }
    dl.end();
    Serial.printf("[Audio] Downloaded %u bytes (expected %d)\n",
                  (unsigned)wavData.size(), len);
    return wavData.size() > 44;
}

bool I2SAmplifier::parseWavHeader(size_t& dataOffset, size_t& dataSize) {
    if (wavData.size() < 12) return false;
    if (memcmp(&wavData[0], "RIFF", 4) != 0 || memcmp(&wavData[8], "WAVE", 4) != 0) {
        Serial.println("[Audio] Not a RIFF/WAVE file");
        return false;
    }
    size_t pos = 12;
    bool haveFmt = false, haveData = false;
    while (pos + 8 <= wavData.size()) {
        const uint8_t* id = &wavData[pos];
        uint32_t chunkSize = wavData[pos + 4] | (wavData[pos + 5] << 8) |
                             (wavData[pos + 6] << 16) | ((uint32_t)wavData[pos + 7] << 24);
        size_t body = pos + 8;
        if (memcmp(id, "fmt ", 4) == 0 && body + 16 <= wavData.size()) {
            numChannels   = wavData[body + 2] | (wavData[body + 3] << 8);
            sampleRate    = wavData[body + 4] | (wavData[body + 5] << 8) |
                            (wavData[body + 6] << 16) | ((uint32_t)wavData[body + 7] << 24);
            bitsPerSample = wavData[body + 14] | (wavData[body + 15] << 8);
            haveFmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            dataOffset = body;
            dataSize = chunkSize;
            if (dataOffset + dataSize > wavData.size()) dataSize = wavData.size() - dataOffset;
            haveData = true;
            break;
        }
        pos = body + chunkSize + (chunkSize & 1);
    }
    return haveFmt && haveData;
}

void I2SAmplifier::playUrlBuffered(const char* url) {
    Serial.printf("[Audio] Downloading %s\n", url);
    Serial.printf("[Audio] Free heap: %u\n", ESP.getFreeHeap());

    if (!downloadWav(url)) {
        std::vector<uint8_t>().swap(wavData);
        return;
    }
    size_t dOff = 0, dSize = 0;
    if (!parseWavHeader(dOff, dSize)) {
        Serial.println("[Audio] WAV header parse failed");
        std::vector<uint8_t>().swap(wavData);
        return;
    }
    Serial.printf("[Audio] WAV: %u Hz, %u ch, %u-bit, data=%u bytes (buffered)\n",
                  sampleRate, numChannels, bitsPerSample, (unsigned)dSize);
    if (bitsPerSample != 16) {
        Serial.println("[Audio] Only 16-bit PCM is supported");
        std::vector<uint8_t>().swap(wavData);
        return;
    }
    playPos = dOff;
    dataEnd = dOff + dSize;
    installI2S(sampleRate);
    bufPlaying = true;
}

void I2SAmplifier::updateBuffered() {
    if (!bufPlaying) return;
    const size_t frameBytes = (size_t)numChannels * 2;
    if (playPos + frameBytes > dataEnd) {
        bufPlaying = false;    // done; main loop calls stop() to flush + cleanup
        return;
    }
    int16_t stereo[PLAYBACK_FRAMES_PER_UPDATE * 2];
    int frames = 0;
    while (frames < PLAYBACK_FRAMES_PER_UPDATE && playPos + frameBytes <= dataEnd) {
        int16_t l = (int16_t)(wavData[playPos] | (wavData[playPos + 1] << 8));
        int16_t r = l;
        if (numChannels >= 2) r = (int16_t)(wavData[playPos + 2] | (wavData[playPos + 3] << 8));
        stereo[frames * 2]     = scaleSample(l);
        stereo[frames * 2 + 1] = scaleSample(r);
        playPos += frameBytes;
        frames++;
    }
    if (frames > 0) {
        size_t written = 0;
        i2s_write(I2S_NUM_0, stereo, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

// ===========================================================================
// STREAMING MODE
// ===========================================================================
bool I2SAmplifier::readExact(uint8_t* buf, int n, uint32_t timeoutMs) {
    int got = 0;
    unsigned long t0 = millis();
    while (got < n) {
        int a = netStream->available();
        if (a > 0) {
            int r = netStream->readBytes(buf + got, min(n - got, a));
            got += r;
            t0 = millis();
        } else {
            if (!http.connected() && netStream->available() == 0) return false;
            if (millis() - t0 > timeoutMs) return false;
            delay(1);
        }
    }
    return true;
}

bool I2SAmplifier::skipExact(int n, uint32_t timeoutMs) {
    uint8_t tmp[64];
    int left = n;
    while (left > 0) {
        int chunk = left < (int)sizeof(tmp) ? left : (int)sizeof(tmp);
        if (!readExact(tmp, chunk, timeoutMs)) return false;
        left -= chunk;
    }
    return true;
}

bool I2SAmplifier::parseWavHeaderStreaming() {
    uint8_t b[12];
    if (!readExact(b, 12, 5000)) return false;
    if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
        Serial.println("[Audio] Not a RIFF/WAVE stream");
        return false;
    }
    bool haveFmt = false;
    for (int i = 0; i < 16; i++) {
        uint8_t ch[8];
        if (!readExact(ch, 8, 5000)) return false;
        uint32_t sz = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[64];
            uint32_t toRead = sz > sizeof(fmt) ? sizeof(fmt) : sz;
            if (!readExact(fmt, toRead, 5000)) return false;
            numChannels   = fmt[2] | (fmt[3] << 8);
            sampleRate    = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            bitsPerSample = fmt[14] | (fmt[15] << 8);
            if (sz > toRead) skipExact(sz - toRead, 5000);
            haveFmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            pcmRemaining = (long)sz;
            return haveFmt;
        } else {
            if (!skipExact(sz + (sz & 1), 5000)) return false;
        }
    }
    return false;
}

void I2SAmplifier::playUrlStreaming(const char* url) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Audio] WiFi not connected");
        return;
    }
    Serial.printf("[Audio] Streaming %s\n", url);
    Serial.printf("[Audio] Free heap: %u\n", ESP.getFreeHeap());

    if (!http.begin(url)) {
        Serial.println("[Audio] http.begin failed");
        return;
    }
    http.setTimeout(10000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Audio] HTTP GET returned %d\n", code);
        http.end();
        return;
    }
    netStream = http.getStreamPtr();
    if (!parseWavHeaderStreaming()) {
        Serial.println("[Audio] WAV header parse failed");
        http.end();
        netStream = nullptr;
        return;
    }
    Serial.printf("[Audio] WAV: %u Hz, %u ch, %u-bit, data=%ld bytes (streaming)\n",
                  sampleRate, numChannels, bitsPerSample, pcmRemaining);
    if (bitsPerSample != 16) {
        Serial.println("[Audio] Only 16-bit PCM is supported");
        http.end();
        netStream = nullptr;
        return;
    }
    installI2S(sampleRate);
    streaming = true;
}

void I2SAmplifier::updateStreaming() {
    if (!streaming) return;
    const size_t frameBytes = (size_t)numChannels * 2;
    if (pcmRemaining < (long)frameBytes) {
        streaming = false;     // done; main loop calls stop() to flush + cleanup
        return;
    }
    int avail = netStream->available();
    if (avail <= 0) {
        if (!http.connected()) streaming = false;  // stream ended early
        return;                                     // DMA keeps draining
    }
    uint8_t buf[READ_CHUNK];
    long want = pcmRemaining < (long)sizeof(buf) ? pcmRemaining : (long)sizeof(buf);
    int toRead = avail < want ? avail : (int)want;
    toRead -= (toRead % frameBytes);
    if (toRead <= 0) return;

    int r = netStream->readBytes(buf, toRead);
    if (r <= 0) return;
    pcmRemaining -= r;

    int16_t stereo[READ_CHUNK];
    int out = 0;
    int frames = r / (int)frameBytes;
    for (int f = 0; f < frames; f++) {
        const uint8_t* p = buf + f * frameBytes;
        int16_t l = (int16_t)(p[0] | (p[1] << 8));
        int16_t rr = l;
        if (numChannels >= 2) rr = (int16_t)(p[2] | (p[3] << 8));
        stereo[out++] = scaleSample(l);
        stereo[out++] = scaleSample(rr);
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, stereo, out * sizeof(int16_t), &written, portMAX_DELAY);
}

// ===========================================================================
// Dispatch + shared entry points
// ===========================================================================
void I2SAmplifier::playUrl(const char* url) {
    stop();
    if (streamMode) playUrlStreaming(url);
    else            playUrlBuffered(url);
}

void I2SAmplifier::playDingDong() {
    installI2S(16000);

    int sample_rate = 16000;
    float amplitude = 16000.0 * volume; // half of max 16-bit range, scaled by volume
    size_t bytes_written;

    int samples1 = (sample_rate * 100) / 1000; // "Ding" 800 Hz 100 ms
    for (int i = 0; i < samples1; i++) {
        int16_t sample = (int16_t)(amplitude * sin(2.0 * PI * 800 * i / sample_rate));
        int32_t sample32 = ((uint16_t)sample << 16) | (uint16_t)sample;
        i2s_write(I2S_NUM_0, &sample32, sizeof(sample32), &bytes_written, portMAX_DELAY);
    }
    int samples2 = (sample_rate * 150) / 1000; // "Dong" 1000 Hz 150 ms
    for (int i = 0; i < samples2; i++) {
        int16_t sample = (int16_t)(amplitude * sin(2.0 * PI * 1000 * i / sample_rate));
        int32_t sample32 = ((uint16_t)sample << 16) | (uint16_t)sample;
        i2s_write(I2S_NUM_0, &sample32, sizeof(sample32), &bytes_written, portMAX_DELAY);
    }
    flushAndUninstall();
}

void I2SAmplifier::update() {
    if (streamMode) updateStreaming();
    else            updateBuffered();
}

void I2SAmplifier::stop() {
    flushAndUninstall();
    // buffered cleanup
    bufPlaying = false;
    playPos = 0;
    dataEnd = 0;
    std::vector<uint8_t>().swap(wavData);
    // streaming cleanup
    streaming = false;
    pcmRemaining = 0;
    netStream = nullptr;
    http.end(); // safe even if not begun
}

bool I2SAmplifier::isPlaying() {
    return streamMode ? streaming : bufPlaying;
}

void I2SAmplifier::debugAudio() {
    if (streamMode && streaming) {
        Serial.printf("[Audio] Streaming, %ld PCM bytes left\n", pcmRemaining);
    } else if (!streamMode && bufPlaying) {
        Serial.printf("[Audio] Playing %u/%u bytes\n", (unsigned)playPos, (unsigned)dataEnd);
    }
}
