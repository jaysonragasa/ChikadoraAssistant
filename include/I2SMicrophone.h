#pragma once
#include "IAudioInput.h"
#include <driver/i2s.h>
#include <vector>

class I2SMicrophone : public IAudioInput {
private:
    int bclkPin;
    int wsPin;
    int dataPin;

    bool recording;
    std::vector<uint8_t> audioBuffer;
    size_t bufferCap = 0; // actual reserved capacity for this recording session
    float micGain = 6.0f; // gain over unity (1.0 = 24-bit full scale -> 16-bit)
    // DC-blocking high-pass state, removes the INMP441's DC bias/rumble so
    // speech stays sharp and headroom isn't wasted on an offset.
    float dcPrevIn = 0.0f;
    float dcPrevOut = 0.0f;
    
    // Max buffer size: ~4.5 seconds of 16kHz 16-bit Mono = 144,000 bytes
    const size_t maxBufferSize = 144000; 

public:
    I2SMicrophone(int bclk, int ws, int data);
    ~I2SMicrophone() override;

    void initialize() override;
    void startRecording() override;
    void update() override;
    void stopRecording() override;

    uint8_t* getAudioData() override;
    size_t getAudioSize() override;
    bool isRecording() override;
    void releaseBuffer() override;
    void setGain(float gain) override;
};
