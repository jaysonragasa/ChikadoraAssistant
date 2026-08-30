#pragma once
#include <Arduino.h>

class IAudioInput {
public:
    virtual ~IAudioInput() = default;
    virtual void initialize() = 0;
    virtual void startRecording() = 0;
    virtual void update() = 0; 
    virtual void stopRecording() = 0;
    
    virtual uint8_t* getAudioData() = 0;
    virtual size_t getAudioSize() = 0;
    virtual bool isRecording() = 0;

    // Fully release the recording buffer's heap memory. Call this once the
    // recorded PCM has been consumed (e.g. after transcription) so large,
    // no-longer-needed buffers don't starve audio playback on the ESP32-C3.
    virtual void releaseBuffer() {}

    // Software gain multiplier applied to captured samples. The INMP441 output
    // is quiet, so >1.0 boosts loudness (with clamping to avoid clipping wrap).
    virtual void setGain(float gain) {}
};
