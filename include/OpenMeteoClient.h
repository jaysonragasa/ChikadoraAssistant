#pragma once
#include "IWeatherProvider.h"

// Fetches weather from the free, keyless Open-Meteo API (https://open-meteo.com/).
class OpenMeteoClient : public IWeatherProvider {
public:
    ~OpenMeteoClient() override = default;
    bool fetch(float lat, float lon, WeatherData& out) override;
};
