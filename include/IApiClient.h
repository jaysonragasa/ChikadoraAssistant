#pragma once
#include <Arduino.h>

class IApiClient {
public:
    virtual ~IApiClient() = default;
    
    virtual String transcribeAudio(uint8_t* pcmData, size_t dataSize) = 0;
    // Send text to the server's chat endpoint and return the assistant reply.
    // Returns "" on failure (no key configured, network error, etc.).
    virtual String chat(String prompt) = 0;
    virtual String submitTtsJob(String text) = 0;
    virtual int isTtsJobDone(String jobId) = 0;
    virtual String getAudioUrl(String jobId) = 0;

    // Point the client at a TTS server address (rebuilds the base URL). Called
    // at boot with the value persisted in NVS so it can be changed without
    // reflashing.
    virtual void setServer(const String& ip, uint16_t port) = 0;

    // Quick liveness check against the server (GET /api/health, short timeout).
    // Returns true if the server answered 200. Used for the boot "connecting to
    // server" screen so it can poll without blocking for long.
    virtual bool ping() = 0;

    // Voice used for TTS synthesis. Set from the device's stored settings so the
    // configured voice takes effect without reflashing.
    virtual void setVoiceId(const String& voice) = 0;
};
