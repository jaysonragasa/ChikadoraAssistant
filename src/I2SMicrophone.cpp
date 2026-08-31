#include "I2SMicrophone.h"

I2SMicrophone::I2SMicrophone(int bclk, int ws, int data)
    : bclkPin(bclk), wsPin(ws), dataPin(data), recording(false) {
    // NOTE: Do NOT reserve maxBufferSize here. Reserving 144 KB at construction
    // holds it for the life of the object and starves ESP32-audioI2S of the
    // heap it needs to buffer playback (causing an OOM store-fault crash on the
    // no-PSRAM C3). We reserve in startRecording() and free in releaseBuffer().
}

I2SMicrophone::~I2SMicrophone() {
    stopRecording();
}

void I2SMicrophone::initialize() {
    // We defer I2S installation to startRecording to avoid conflicts with I2SAmplifier
    // on the ESP32-C3 which only has one I2S port (I2S_NUM_0).
}

void I2SMicrophone::startRecording() {
    // Release any existing driver (e.g., from ESP32-audioI2S)
    i2s_driver_uninstall(I2S_NUM_0);
    
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 uses 32-bit slots
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
    
    // Reserve up front so recording never reallocates/grows mid-capture. We
    // pre-check the largest CONTIGUOUS free block first: if we blindly called
    // reserve() when the heap is fragmented, operator new would throw
    // std::bad_alloc, and the C3's C++ exception unwinder crashes on a throw
    // (hard reboot) instead of recovering. So cap to what safely fits.
    audioBuffer.clear();
    const size_t MARGIN = 24 * 1024; // headroom for WiFi/HTTP during the upload
    size_t largest = ESP.getMaxAllocHeap();
    size_t safeCap = maxBufferSize;
    if (largest < maxBufferSize + MARGIN) {
        safeCap = (largest > MARGIN) ? (largest - MARGIN) : 0;
        safeCap -= (safeCap % 2); // keep 16-bit sample alignment
        Serial.printf("Mic: Low contiguous heap (%u B); capping record buffer to %u B\n",
                      (unsigned)largest, (unsigned)safeCap);
    }
    bufferCap = safeCap;
    if (bufferCap >= 320) {
        audioBuffer.reserve(bufferCap);
    }
    // Reset the DC blocker so a stale offset doesn't thump at the start.
    dcPrevIn = 0.0f;
    dcPrevOut = 0.0f;
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

    // Drain the I2S DMA completely on each call. Reading only one small chunk
    // per loop iteration (with the loop's ~10 ms delay) falls behind the 16 kHz
    // capture rate, so the DMA overflows and drops samples - heard as pops and
    // clicks. Keep reading until the DMA is empty so we always stay real-time.
    int32_t tempBuf[128];
    while (audioBuffer.size() < bufferCap) {
        size_t bytes_read = 0;
        esp_err_t result = i2s_read(I2S_NUM_0, &tempBuf, sizeof(tempBuf), &bytes_read, 0);
        if (result != ESP_OK || bytes_read == 0) break;   // nothing left to read

        int samples = bytes_read / 4;
        for (int i = 0; i < samples; i++) {
            // The INMP441 puts its 24-bit sample in the top bits of the 32-bit
            // slot. Extract the full 24-bit value (arithmetic shift keeps sign)
            // so we keep resolution while applying gain.
            int32_t s24 = tempBuf[i] >> 8;   // signed, +/- 2^23

            // DC blocker (one-pole high-pass ~13 Hz): removes the mic's DC bias
            // and subsonic rumble that otherwise dull the sound and clip early.
            float in = (float)s24;
            float hp = in - dcPrevIn + 0.995f * dcPrevOut;
            dcPrevIn = in;
            dcPrevOut = hp;

            // Apply gain at full resolution, then scale 24-bit -> 16-bit.
            int32_t sample = (int32_t)(hp * micGain / 256.0f);

            // Clamp so loud input distorts gracefully instead of wrapping.
            if (sample > 32767) sample = 32767;
            else if (sample < -32768) sample = -32768;
            int16_t sample16 = (int16_t)sample;

            audioBuffer.push_back(sample16 & 0xFF);          // little-endian
            audioBuffer.push_back((sample16 >> 8) & 0xFF);
            if (audioBuffer.size() >= bufferCap) break;
        }

        // A short read means the DMA is drained for now.
        if (bytes_read < sizeof(tempBuf)) break;
    }
}

void I2SMicrophone::stopRecording() {
    if (recording) {
        recording = false;
        i2s_driver_uninstall(I2S_NUM_0); // Free the port for the speaker
        Serial.printf("Mic: Stopped recording. Total bytes: %u\n", audioBuffer.size());
    }
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
    // swap-with-empty actually frees the vector's capacity (clear() would not).
    std::vector<uint8_t>().swap(audioBuffer);
    Serial.println("Mic: Released audio buffer to free heap for playback.");
}

void I2SMicrophone::setGain(float gain) {
    if (gain < 0.1f) gain = 0.1f; // guard against silence/negative
    micGain = gain;
    Serial.printf("Mic: Gain set to %.1fx\n", micGain);
}
