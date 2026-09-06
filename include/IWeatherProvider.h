#pragma once
#include "WeatherData.h"

// Abstraction for a weather source, so the assistant depends on the interface
// rather than a concrete HTTP/JSON client (Dependency Inversion).
class IWeatherProvider {
public:
    virtual ~IWeatherProvider() = default;

    // Fetch current conditions + a short hourly forecast for the given
    // coordinates into `out`. Returns true on success; on failure `out` is
    // left untouched.
    virtual bool fetch(float lat, float lon, WeatherData& out) = 0;
};
