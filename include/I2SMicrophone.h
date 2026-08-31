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
    bool monitoring = false;   // installed just to sample level (no buffering)
    std::vector<uint8_t> audioBuffer;
    size_t bufferCap = 0; // actual reserved capacity for this recording session
    float micGain = 6.0f; // gain over unity (1.0 = 24-bit full scale -> 16-bit)
    // DC-blocking high-pass state, removes the INMP441's DC bias/rumble so
    // speech stays sharp and headroom isn't wasted on an offset.
    float dcPrevIn = 0.0f;
    float dcPrevOut = 0.0f;

    // End-of-speech (silence) auto-stop, configured via setSilenceStop().
    bool          silenceStopEnabled = false;
    int           silenceThreshold = 1400;
    unsigned long silenceMs = 900;
    unsigned long minSpeechMs = 400;
    unsigned long recordStartMs = 0;
    unsigned long lastVoiceMs = 0;
    bool          sawSpeech = false;

    void installRxDriver();  // shared by startRecording() and startMonitoring()
    int  applyGainClamp(int32_t s24); // 24-bit sample -> gained/clamped 16-bit
    
    // Target max recording length: ~8 seconds of 16kHz 16-bit mono = 256,000
    // bytes, so longer questions aren't cut off. startRecording() pre-checks the
    // heap and shrinks this to whatever safely fits (the C3 may not have 256 KB
    // contiguous free), so effective length can be a bit less. Tap again to stop
    // early.
    const size_t maxBufferSize = 256000; 

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

    void startMonitoring() override;
    int  readPeakLevel() override;
    void stopMonitoring() override;
    void setSilenceStop(bool enabled, int threshold,
                        unsigned long silenceMs, unsigned long minSpeechMs) override;
};
