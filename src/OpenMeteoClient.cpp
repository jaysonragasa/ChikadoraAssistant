#include "OpenMeteoClient.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

bool OpenMeteoClient::fetch(float lat, float lon, WeatherData& out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // Ask only for what we render, and cap the hourly list to keep the JSON
    // (and the parse buffer) small on the MCU.
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) +
                 "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,weather_code" +
                 "&hourly=temperature_2m,weather_code" +
                 "&forecast_hours=6&timezone=auto";

    WiFiClientSecure client;
    client.setInsecure();          // public data; skip cert pinning to save flash/RAM
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Weather] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Read the whole (small) body first, then parse. Parsing directly off the
    // TLS stream proved flaky (partial reads yielded zeros); a String is safe
    // here because forecast_hours is capped.
    String payload = http.getString();
    http.end();
    Serial.printf("[Weather] HTTP %d, %d bytes\n", code, payload.length());

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[Weather] JSON error: ");
        Serial.println(err.c_str());
        Serial.println("[Weather] body: " + payload.substring(0, 160));
        return false;
    }

    // Open-Meteo signals bad requests with {"error":true,"reason":"..."}.
    if (doc["error"].as<bool>()) {
        Serial.println("[Weather] API error: " + doc["reason"].as<String>());
        return false;
    }
    if (!doc["current"].is<JsonObject>()) {
        Serial.println("[Weather] no 'current' in response: " + payload.substring(0, 160));
        return false;
    }

    WeatherData w;   // start fresh
    w.currentTemp = doc["current"]["temperature_2m"] | 0.0f;
    w.currentCode = doc["current"]["weather_code"] | 0;

    JsonArray temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
    JsonArray codes = doc["hourly"]["weather_code"].as<JsonArray>();
    JsonArray times = doc["hourly"]["time"].as<JsonArray>();
    int n = 0;
    for (size_t i = 0; i < temps.size() && n < WeatherData::MAX_HOURS; i++) {
        w.hours[n].temp = temps[i] | 0.0f;
        w.hours[n].code = (i < codes.size()) ? (int)(codes[i] | 0) : 0;
        const char* t = (i < times.size()) ? (times[i] | "") : "";
        // "YYYY-MM-DDTHH:MM" -> hour is chars [11..12]
        w.hours[n].hour = (strlen(t) >= 13) ? (t[11] - '0') * 10 + (t[12] - '0') : 0;
        n++;
    }
    w.hourCount = n;
    w.valid = true;

    out = w;
    Serial.printf("[Weather] %.1fC code=%d, %d hourly\n", out.currentTemp, out.currentCode, out.hourCount);
    return true;
}
