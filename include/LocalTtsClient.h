#pragma once
#include "IApiClient.h"
#include "RuntimeSettings.h"

class LocalTtsClient : public IApiClient {
private:
    String serverIp;
    uint16_t kokoroPort;
    uint16_t moonshinePort;
    uint16_t ollamaPort;
    String voiceId;
    String ollamaModel;
    String ollamaSystemPrompt;
    bool   tarsEnabled;
    int    tarsHumor;
    int    tarsHonesty;
    int    tarsSarcasm;
    int    tarsDiscretion;

    String currentTtsUrl;

public:
    LocalTtsClient(String ip, int port, String voice);
    ~LocalTtsClient() override = default;

    String transcribeAudio(uint8_t* pcmData, size_t dataSize) override;
    String chat(String prompt) override;
    String submitTtsJob(String text) override;
    int isTtsJobDone(String jobId) override;
    String getAudioUrl(String jobId) override;
    void setServer(const String& ip, uint16_t port) override;
    void configure(const String& ip, const RuntimeSettings& s) override;
    ServerStatus ping() override;
    void setVoiceId(const String& voice) override;
};
