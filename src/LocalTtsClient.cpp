#include "LocalTtsClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Helper to URL-encode strings for x-www-form-urlencoded POST requests
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
    : serverIp(ip), serverPort(port), voiceId(voice) {
    baseUrl = "http://" + serverIp + ":" + String(serverPort);
}

String LocalTtsClient::transcribeAudio(uint8_t* pcmData, size_t dataSize) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] Error: WiFi not connected");
        return "";
    }

    WiFiClient client;
    Serial.printf("[API] Connecting to %s:%d...\n", serverIp.c_str(), serverPort);
    if (!client.connect(serverIp.c_str(), serverPort)) {
        Serial.println("[API] Error: Failed to connect to server! Is uvicorn running with --host 0.0.0.0?");
        return "";
    }

    String boundary = "----ESP32Boundary123456";
    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\n";
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

    client.println("POST /api/transcribe HTTP/1.1");
    client.println("Host: " + serverIp);
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.print("Content-Length: ");
    client.println(contentLength);
    client.println();

    client.print(head);
    client.write(header, 44);
    
    // Stream audio data
    size_t sent = 0;
    while(sent < dataSize) {
        size_t toSend = (dataSize - sent > 1024) ? 1024 : (dataSize - sent);
        client.write(pcmData + sent, toSend);
        sent += toSend;
    }
    client.print(tail);

    Serial.println("[API] Sent POST request. Waiting for response...");
    
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 10000) { // 10 sec timeout
            Serial.println("[API] Error: Timeout waiting for response!");
            client.stop();
            return "";
        }
        delay(10);
    }
    
    String responseBody = "";
    bool isBody = false;
    Serial.println("\n--- RAW HTTP RESPONSE ---");
    while (client.available()) {
        String line = client.readStringUntil('\n');
        Serial.println(line); // Print raw HTTP headers and body to debug
        if (line == "\r") { isBody = true; continue; }
        if (isBody) responseBody += line;
    }
    client.stop();
    Serial.println("-------------------------");

    Serial.println("[API] Response Body: " + responseBody);

    // Parse JSON: {"text": "..."}
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, responseBody);
    if (!error && doc.containsKey("text")) {
        return doc["text"].as<String>();
    } else {
        Serial.print("[API] JSON Parse Error: ");
        Serial.println(error.c_str());
    }
    return "";
}

String LocalTtsClient::submitTtsJob(String text) {
    if (WiFi.status() != WL_CONNECTED) return "";

    HTTPClient http;
    http.begin(baseUrl + "/api/synthesize");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // We MUST specify language=a (American English) otherwise the server defaults to "Auto",
    // which causes the Kokoro TTS engine to crash since it expects 'a', 'b', 'f', etc.
    String postData = "model_id=kokoro-82m&text=" + urlEncode(text) + "&voice=" + urlEncode(voiceId) + "&language=a";
    
    int httpCode = http.POST(postData);
    if (httpCode > 0) {
        if (httpCode == 200 || httpCode == 201) {
            String payload = http.getString();
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);
            if (!error && doc.containsKey("id")) {
                String id = doc["id"].as<String>();
                http.end();
                return id;
            } else {
                Serial.print("[API] JSON Parse Error: ");
                Serial.println(error.c_str());
            }
        } else {
            Serial.printf("[API] Server returned HTTP %d: %s\n", httpCode, http.getString().c_str());
        }
    } else {
        Serial.printf("[API] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return "";
}

String LocalTtsClient::chat(String prompt) {
    if (WiFi.status() != WL_CONNECTED) return "";

    HTTPClient http;
    http.begin(baseUrl + "/api/chat");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    // Gemini can take a few seconds to respond; give it room before timing out.
    http.setTimeout(20000);

    String postData = "text=" + urlEncode(prompt);

    int httpCode = http.POST(postData);
    String reply = "";
    if (httpCode == 200) {
        String payload = http.getString();
        // Replies can be a few sentences; size the doc generously.
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error && doc.containsKey("reply")) {
            reply = doc["reply"].as<String>();
        } else {
            Serial.print("[API] Chat JSON parse error: ");
            Serial.println(error.c_str());
        }
    } else if (httpCode > 0) {
        Serial.printf("[API] Chat server returned HTTP %d: %s\n",
                      httpCode, http.getString().c_str());
    } else {
        Serial.printf("[API] Chat POST failed, error: %s\n",
                      http.errorToString(httpCode).c_str());
    }

    http.end();
    return reply;
}

int LocalTtsClient::isTtsJobDone(String jobId) {
    if (WiFi.status() != WL_CONNECTED) return -1;

    HTTPClient http;
    http.begin(baseUrl + "/api/jobs/" + jobId);
    int httpCode = http.GET();
    int result = 0; // default: pending
    
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, payload)) {
            String state = doc["state"].as<String>();
            bool hasResult = doc["has_result"].as<bool>();
            if (state == "done" && hasResult) {
                result = 1;
            } else if (state == "error" || state == "canceled") {
                result = -1;
                if (doc.containsKey("error") && !doc["error"].isNull()) {
                    Serial.println("[API] Server job failed with error: " + doc["error"].as<String>());
                }
            }
        }
    } else if (httpCode == 404) {
        // Job not found - server might have restarted or deleted it
        result = -1;
    } else if (httpCode < 0) {
        // Connection error
        result = -1;
    }
    http.end();
    return result;
}

String LocalTtsClient::getAudioUrl(String jobId) {
    return baseUrl + "/api/jobs/" + jobId + "/result";
}
