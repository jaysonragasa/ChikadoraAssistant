#include "LocalTtsClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Helper to URL-encode strings for HTTP GET/POST query params
String urlEncode(String str) {
    String encodedString = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedString += c;
        } else if (c == ' ') {
            encodedString += '+';
        } else {
            char buf[4];
            sprintf(buf, "%%%02X", (unsigned char)c);
            encodedString += buf;
        }
    }
    return encodedString;
}

LocalTtsClient::LocalTtsClient(String ip, int port, String voice) 
    : serverIp(ip), 
      kokoroPort(Config::Server::KOKORO_PORT),
      moonshinePort(Config::Server::MOONSHINE_PORT),
      ollamaPort(Config::Server::OLLAMA_PORT),
      voiceId(voice),
      ollamaModel(Config::Server::OLLAMA_MODEL),
      ollamaSystemPrompt(Config::Server::SYSTEM_PROMPT),
      tarsEnabled(Config::Personality::TARS_ENABLED),
      tarsHumor(Config::Personality::TARS_HUMOR),
      tarsHonesty(Config::Personality::TARS_HONESTY),
      tarsSarcasm(Config::Personality::TARS_SARCASM),
      tarsDiscretion(Config::Personality::TARS_DISCRETION) {}

void LocalTtsClient::configure(const String& ip, const RuntimeSettings& s) {
    serverIp = ip;
    kokoroPort = s.kokoroPort;
    moonshinePort = s.moonshinePort;
    ollamaPort = s.ollamaPort;
    voiceId = s.voiceId;
    ollamaModel = s.ollamaModel;
    ollamaSystemPrompt = s.ollamaSystemPrompt;
    tarsEnabled = s.tarsEnabled;
    tarsHumor = s.tarsHumor;
    tarsHonesty = s.tarsHonesty;
    tarsSarcasm = s.tarsSarcasm;
    tarsDiscretion = s.tarsDiscretion;
    Serial.printf("[API] Configured AI phone at %s (Kokoro:%u Moonshine:%u Ollama:%u)\n",
                  serverIp.c_str(), kokoroPort, moonshinePort, ollamaPort);
}

void LocalTtsClient::setServer(const String& ip, uint16_t port) {
    serverIp = ip;
    if (port > 0) kokoroPort = port;
}

void LocalTtsClient::setVoiceId(const String& voice) {
    if (voice.length() > 0) voiceId = voice;
}

// -----------------------------------------------------------------------------
// STT: Send recorded WAV directly to Moonshine (/transcribe)
// -----------------------------------------------------------------------------
String LocalTtsClient::transcribeAudio(uint8_t* pcmData, size_t dataSize) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] Error: WiFi not connected");
        return "";
    }

    WiFiClient client;
    Serial.printf("[API] Connecting to Moonshine at %s:%d...\n", serverIp.c_str(), moonshinePort);
    client.setTimeout(15000); // 15 seconds (Arduino setTimeout is in milliseconds!)
    if (!client.connect(serverIp.c_str(), moonshinePort)) {
        Serial.println("[API] Error: Failed to connect to Moonshine!");
        return "";
    }

    String boundary = "----ESP32Boundary123456";
    // Moonshine FastAPI expects the multipart field name to be "file"
    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";

    uint32_t wavSize = dataSize + 36;
    uint32_t byteRate = 16000 * 2;
    uint8_t header[44] = {
        'R', 'I', 'F', 'F', 
        (uint8_t)(wavSize & 0xff), (uint8_t)((wavSize >> 8) & 0xff), (uint8_t)((wavSize >> 16) & 0xff), (uint8_t)((wavSize >> 24) & 0xff),
        'W', 'A', 'V', 'E', 
        'f', 'm', 't', ' ', 
        16, 0, 0, 0, 1, 0, 1, 0, 
        (uint8_t)(16000 & 0xff), (uint8_t)((16000 >> 8) & 0xff), 0, 0, 
        (uint8_t)(byteRate & 0xff), (uint8_t)((byteRate >> 8) & 0xff), 0, 0, 
        2, 0, 16, 0, 
        'd', 'a', 't', 'a', 
        (uint8_t)(dataSize & 0xff), (uint8_t)((dataSize >> 8) & 0xff), (uint8_t)((dataSize >> 16) & 0xff), (uint8_t)((dataSize >> 24) & 0xff)
    };

    uint32_t contentLength = head.length() + 44 + dataSize + tail.length();

    client.println("POST /transcribe HTTP/1.1");
    client.println("Host: " + serverIp + ":" + String(moonshinePort));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.print("Content-Length: ");
    client.println(contentLength);
    client.println("Connection: close");
    client.println();

    client.print(head);
    client.write(header, 44);
    
    // Stream audio data in 1KB chunks
    size_t sent = 0;
    while (sent < dataSize) {
        size_t toSend = (dataSize - sent > 1024) ? 1024 : (dataSize - sent);
        client.write(pcmData + sent, toSend);
        sent += toSend;
    }
    client.print(tail);

    Serial.println("[API] Sent audio to Moonshine. Waiting for transcription...");
    
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 15000) {
            Serial.println("[API] Error: Timeout waiting for Moonshine transcription!");
            client.stop();
            return "";
        }
        delay(10);
    }
    
    String responseBody = "";
    bool isBody = false;
    unsigned long lastByteAt = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!isBody) {
                if (line == "\r" || line == "") { isBody = true; }
            } else {
                responseBody += line;
            }
            lastByteAt = millis();
        } else {
            if (isBody && responseBody.length() > 0 && (millis() - lastByteAt > 200)) {
                break; // Complete response body received
            }
            if (millis() - lastByteAt > 3000) {
                break; // Stalled
            }
            delay(5);
        }
    }
    client.stop();

    Serial.println("[API] Moonshine Response: " + responseBody);

    // Parse JSON: {"text": "..."} or {"transcription": "..."}
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, responseBody);
    if (!error) {
        if (doc.containsKey("text")) return doc["text"].as<String>();
        if (doc.containsKey("transcription")) return doc["transcription"].as<String>();
    } else {
        Serial.print("[API] JSON Parse Error: ");
        Serial.println(error.c_str());
    }
    return "";
}

// -----------------------------------------------------------------------------
// LLM: Send prompt directly to Ollama (/api/generate) with TARS personality
// -----------------------------------------------------------------------------
String LocalTtsClient::chat(String prompt) {
    if (WiFi.status() != WL_CONNECTED) return "";

    String url = "http://" + serverIp + ":" + String(ollamaPort) + "/api/generate";
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(25000);

    String systemText = ollamaSystemPrompt;
    if (tarsEnabled) {
        systemText += " TARS parameters: Humor " + String(tarsHumor) +
                      "%, Sarcasm " + String(tarsSarcasm) +
                      "%, Honesty " + String(tarsHonesty) +
                      "%, Discretion " + String(tarsDiscretion) +
                      "%. Speak in character.";
    }

    DynamicJsonDocument reqDoc(2048);
    reqDoc["model"]  = ollamaModel;
    reqDoc["prompt"] = prompt;
    reqDoc["system"] = systemText;
    reqDoc["stream"] = false;

    String jsonBody;
    serializeJson(reqDoc, jsonBody);

    Serial.printf("[API] Querying Ollama (%s)...\n", ollamaModel.c_str());
    int httpCode = http.POST(jsonBody);
    String reply = "";
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error && doc.containsKey("response")) {
            reply = doc["response"].as<String>();
        } else {
            Serial.print("[API] Ollama JSON parse error: ");
            Serial.println(error.c_str());
        }
    } else if (httpCode > 0) {
        Serial.printf("[API] Ollama HTTP %d: %s\n", httpCode, http.getString().c_str());
    } else {
        Serial.printf("[API] Ollama POST failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
    return reply;
}

// -----------------------------------------------------------------------------
// TTS: Generate direct Kokoro URL (GET /tts?text=...&voice=...)
// -----------------------------------------------------------------------------
String LocalTtsClient::submitTtsJob(String text) {
    if (WiFi.status() != WL_CONNECTED) return "";
    
    // Direct Kokoro endpoint URL
    currentTtsUrl = "http://" + serverIp + ":" + String(kokoroPort) + 
                    "/tts?text=" + urlEncode(text) + 
                    "&voice=" + urlEncode(voiceId);
    
    Serial.println("[API] Prepared Kokoro direct URL: " + currentTtsUrl);
    return "direct_stream";
}

int LocalTtsClient::isTtsJobDone(String jobId) {
    // Direct stream is ready immediately without polling
    return 1;
}

String LocalTtsClient::getAudioUrl(String jobId) {
    return currentTtsUrl;
}

// -----------------------------------------------------------------------------
// Ping: Fast socket check across Kokoro, Moonshine, and Ollama
// -----------------------------------------------------------------------------
ServerStatus LocalTtsClient::ping() {
    ServerStatus status;
    status.ttsReachable = false;
    status.sttAvailable = false;
    status.ollamaReachable = false;

    if (WiFi.status() != WL_CONNECTED) return status;

    WiFiClient client;
    client.setTimeout(1500); // 1.5-second connect timeout in milliseconds

    // 1. Check Kokoro TTS port
    if (client.connect(serverIp.c_str(), kokoroPort)) {
        status.ttsReachable = true;
        client.stop();
    }

    // 2. Check Moonshine STT port
    if (client.connect(serverIp.c_str(), moonshinePort)) {
        status.sttAvailable = true;
        client.stop();
    }

    // 3. Check Ollama LLM port
    if (client.connect(serverIp.c_str(), ollamaPort)) {
        status.ollamaReachable = true;
        client.stop();
    }

    return status;
}
