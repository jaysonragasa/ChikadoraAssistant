#pragma once
#include <stdint.h>

// Weather categories we render distinct (animated) icons for. Derived from the
// WMO weather codes Open-Meteo returns.
enum class WeatherCategory { Clear, Clouds, Rain, Snow, Storm, Fog };

// Map a WMO weather code to a category. See https://open-meteo.com/ docs.
inline WeatherCategory weatherCategory(int code) {
    if (code == 0) return WeatherCategory::Clear;                 // clear sky
    if (code == 1 || code == 2 || code == 3) return WeatherCategory::Clouds;
    if (code == 45 || code == 48) return WeatherCategory::Fog;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WeatherCategory::Snow;
    if (code >= 95) return WeatherCategory::Storm;               // thunderstorm
    return WeatherCategory::Rain;                                 // drizzle/rain/showers
}

// Spoken-friendly phrase for a weather code (used by the weather voice summary).
inline const char* weatherConditionText(int code) {
    switch (weatherCategory(code)) {
        case WeatherCategory::Clear:  return "clear skies";
        case WeatherCategory::Clouds: return "cloudy skies";
        case WeatherCategory::Rain:   return "rain";
        case WeatherCategory::Snow:   return "snow";
        case WeatherCategory::Storm:  return "thunderstorms";
        case WeatherCategory::Fog:    return "fog";
    }
    return "unknown conditions";
}

// One hour of forecast.
struct HourlyForecast {
    int   hour = 0;    // hour of day, 0-23
    float temp = 0;    // temperature (deg C)
    int   code = 0;    // WMO weather code
};

// Current conditions + a short hourly forecast.
struct WeatherData {
    static const int MAX_HOURS = 6;
    bool  valid       = false;
    float currentTemp = 0;
    int   currentCode = 0;
    HourlyForecast hours[MAX_HOURS];
    int   hourCount   = 0;
    int   utcOffsetSeconds = 0;   // local timezone offset for the coordinates
};
