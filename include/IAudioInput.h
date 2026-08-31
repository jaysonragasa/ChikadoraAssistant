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

    // --- Level monitoring (for voice activation, without recording) ---
    // Install the mic just to sample its level; readPeakLevel() returns the
    // peak 16-bit amplitude (0..32767) of the latest frame, 0 if not monitoring.
    virtual void startMonitoring() {}
    virtual int  readPeakLevel() { return 0; }
    virtual void stopMonitoring() {}

    // Auto-stop recording after `silenceMs` of input below `threshold` (once at
    // least `minSpeechMs` has been recorded). Used by voice mode for hands-free
    // end-of-speech. Disabled by default.
    virtual void setSilenceStop(bool enabled, int threshold,
                                unsigned long silenceMs, unsigned long minSpeechMs) {}
};
