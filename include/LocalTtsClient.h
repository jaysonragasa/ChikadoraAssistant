#pragma once
#include "IApiClient.h"

class LocalTtsClient : public IApiClient {
private:
    String serverIp;
    int serverPort;
    String voiceId;
    String baseUrl;

public:
    LocalTtsClient(String ip, int port, String voice);
    ~LocalTtsClient() override = default;

    String transcribeAudio(uint8_t* pcmData, size_t dataSize) override;
    String chat(String prompt) override;
    String submitTtsJob(String text) override;
    int isTtsJobDone(String jobId) override;
    String getAudioUrl(String jobId) override;
};
