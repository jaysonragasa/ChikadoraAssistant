#pragma once
#include <Arduino.h>

class IApiClient {
public:
    virtual ~IApiClient() = default;
    
    virtual String transcribeAudio(uint8_t* pcmData, size_t dataSize) = 0;
    // Send text to the server's Gemini endpoint and return the assistant reply.
    // Returns "" on failure (no key configured, network error, etc.).
    virtual String chat(String prompt) = 0;
    virtual String submitTtsJob(String text) = 0;
    virtual int isTtsJobDone(String jobId) = 0;
    virtual String getAudioUrl(String jobId) = 0;
};
