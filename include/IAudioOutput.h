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
};
