#pragma once

class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;
    virtual void initialize() = 0;
    virtual void playUrl(const char* url) = 0;
    virtual void playDingDong() = 0;
    virtual void update() = 0; 
    virtual void stop() = 0;
    virtual bool isPlaying() = 0;
    virtual void debugAudio() {}

    // Choose playback strategy: false = download the whole clip then play from
    // RAM (smooth, needs the clip to fit in memory); true = stream from HTTP and
    // play as it arrives (any length, but can stutter on a busy/slow network).
    virtual void setStreamingMode(bool /*enabled*/) {}

    // Output volume: 0.0 = silent, 1.0 = full scale (unchanged). Values above
    // 1.0 amplify and may clip. Scales samples in software before I2S.
    virtual void setVolume(float /*volume*/) {}

    // Hold the amp's data line low so it outputs digital silence. Used while
    // idle voice-monitoring keeps the shared I2S clock running, which would
    // otherwise make the amp hiss static from a floating data pin.
    virtual void idleMute() {}
};
