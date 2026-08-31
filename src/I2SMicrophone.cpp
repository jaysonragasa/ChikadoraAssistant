#include "I2SMicrophone.h"

I2SMicrophone::I2SMicrophone(int bclk, int ws, int data)
    : bclkPin(bclk), wsPin(ws), dataPin(data), recording(false) {
    // NOTE: Do NOT reserve maxBufferSize here. Reserving at construction holds
    // the memory for the object's life and starves audio playback on the
    // no-PSRAM C3. We reserve in startRecording() and free in releaseBuffer().
}

I2SMicrophone::~I2SMicrophone() {
    stopRecording();
}

void I2SMicrophone::initialize() {
    // I2S is installed on demand (startRecording / startMonitoring) since the
    // mic shares the single I2S_NUM_0 port with the amplifier.
}

// Shared I2S RX setup for the INMP441 (16 kHz, 32-bit slots, left channel).
void I2SMicrophone::installRxDriver() {
    i2s_driver_uninstall(I2S_NUM_0); // release whatever held the port

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = bclkPin,
        .ws_io_num = wsPin,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = dataPin
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

// Convert a 24-bit INMP441 sample to a gained, clamped 16-bit value.
int I2SMicrophone::applyGainClamp(int32_t s24) {
    int32_t s = (int32_t)((float)s24 * micGain / 256.0f);
    if (s > 32767) s = 32767;
    else if (s < -32768) s = -32768;
    return (int)s;
}

void I2SMicrophone::startRecording() {
    monitoring = false;
    installRxDriver();

    // Reserve up front (pre-checking contiguous heap so reserve() can't throw
    // std::bad_alloc, which would hard-reboot the C3). Cap to what safely fits.
    audioBuffer.clear();
    const size_t MARGIN = 24 * 1024;
    size_t largest = ESP.getMaxAllocHeap();
    size_t safeCap = maxBufferSize;
    if (largest < maxBufferSize + MARGIN) {
        safeCap = (largest > MARGIN) ? (largest - MARGIN) : 0;
        safeCap -= (safeCap % 2);
        Serial.printf("Mic: Low contiguous heap (%u B); capping record buffer to %u B\n",
                      (unsigned)largest, (unsigned)safeCap);
    }
    bufferCap = safeCap;
    if (bufferCap >= 320) {
        audioBuffer.reserve(bufferCap);
    }

    dcPrevIn = 0.0f;
    dcPrevOut = 0.0f;

    unsigned long now = millis();
    recordStartMs = now;
    lastVoiceMs = now;
    sawSpeech = false;

    recording = true;
    Serial.printf("Mic: Started recording (cap %u B, ~%.1fs)...\n",
                  (unsigned)bufferCap, bufferCap / 32000.0f);
}

void I2SMicrophone::update() {
    if (!recording) return;

    if (audioBuffer.size() >= bufferCap) {
        Serial.println("Mic: Buffer full, stopping automatically.");
        stopRecording();
        return;
    }

    size_t bytes_read = 0;
    int32_t tempBuf[128];
    esp_err_t result = i2s_read(I2S_NUM_0, &tempBuf, sizeof(tempBuf), &bytes_read, 0);

    int framePeak = 0;
    if (result == ESP_OK && bytes_read > 0) {
        int samples = bytes_read / 4;
        for (int i = 0; i < samples; i++) {
            int32_t s24 = tempBuf[i] >> 8;   // signed 24-bit

            // DC blocker (one-pole high-pass ~13 Hz) keeps speech sharp.
            float in = (float)s24;
            float hp = in - dcPrevIn + 0.995f * dcPrevOut;
            dcPrevIn = in;
            dcPrevOut = hp;

            int32_t sample = (int32_t)(hp * micGain / 256.0f);
            if (sample > 32767) sample = 32767;
            else if (sample < -32768) sample = -32768;
            int16_t sample16 = (int16_t)sample;

            int mag = sample16 < 0 ? -(int)sample16 : (int)sample16;
            if (mag > framePeak) framePeak = mag;

            audioBuffer.push_back(sample16 & 0xFF);
            audioBuffer.push_back((sample16 >> 8) & 0xFF);
            if (audioBuffer.size() >= bufferCap) break;
        }
    }

    // End-of-speech: stop after enough trailing silence (voice mode).
    if (silenceStopEnabled) {
        unsigned long now = millis();
        if (framePeak >= silenceThreshold) {
            lastVoiceMs = now;
            sawSpeech = true;
        }
        if (sawSpeech && (now - recordStartMs) >= minSpeechMs &&
            (now - lastVoiceMs) >= silenceMs) {
            Serial.println("Mic: Silence detected, stopping automatically.");
            stopRecording();
        }
    }
}

void I2SMicrophone::stopRecording() {
    if (recording) {
        recording = false;
        i2s_driver_uninstall(I2S_NUM_0); // free the port for the speaker
        Serial.printf("Mic: Stopped recording. Total bytes: %u\n", audioBuffer.size());
    }
}

// --- Level monitoring (voice activation) ---
void I2SMicrophone::startMonitoring() {
    if (monitoring) return;
    recording = false;
    installRxDriver();
    monitoring = true;
}

int I2SMicrophone::readPeakLevel() {
    if (!monitoring) return 0;
    size_t bytes_read = 0;
    int32_t tempBuf[128];
    esp_err_t result = i2s_read(I2S_NUM_0, &tempBuf, sizeof(tempBuf), &bytes_read, 0);
    if (result != ESP_OK || bytes_read < 4) return 0;
    int samples = bytes_read / 4;

    // Remove DC first (the INMP441 has a large bias) by subtracting the frame
    // mean, THEN apply gain - same order as recording. Otherwise the DC offset
    // dominates and the "level" no longer reflects actual sound.
    long sum = 0;
    for (int i = 0; i < samples; i++) sum += (tempBuf[i] >> 8);
    int32_t mean = (int32_t)(sum / samples);

    int peak = 0;
    for (int i = 0; i < samples; i++) {
        int32_t ac = (tempBuf[i] >> 8) - mean;
        int32_t g = (int32_t)((float)ac * micGain / 256.0f);
        if (g > 32767) g = 32767;
        else if (g < -32768) g = -32768;
        int m = g < 0 ? -(int)g : (int)g;
        if (m > peak) peak = m;
    }
    return peak;
}

void I2SMicrophone::stopMonitoring() {
    if (!monitoring) return;
    monitoring = false;
    i2s_driver_uninstall(I2S_NUM_0);
}

void I2SMicrophone::setSilenceStop(bool enabled, int threshold,
                                   unsigned long sMs, unsigned long minMs) {
    silenceStopEnabled = enabled;
    silenceThreshold = threshold;
    silenceMs = sMs;
    minSpeechMs = minMs;
}

uint8_t* I2SMicrophone::getAudioData() {
    return audioBuffer.data();
}

size_t I2SMicrophone::getAudioSize() {
    return audioBuffer.size();
}

bool I2SMicrophone::isRecording() {
    return recording;
}

void I2SMicrophone::releaseBuffer() {
    std::vector<uint8_t>().swap(audioBuffer);
    Serial.println("Mic: Released audio buffer to free heap for playback.");
}

void I2SMicrophone::setGain(float gain) {
    if (gain < 0.1f) gain = 0.1f;
    micGain = gain;
    Serial.printf("Mic: Gain set to %.1fx\n", micGain);
}
