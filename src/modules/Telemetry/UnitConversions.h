#pragma once

#include <cmath>

class UnitConversions
{
  public:
    static float CelsiusToFahrenheit(float celsius);
    static float MetersPerSecondToKnots(float metersPerSecond);
    static float MetersPerSecondToMilesPerHour(float metersPerSecond);
    static float HectoPascalToInchesOfMercury(float hectoPascal);

    // Bound a float before Arduino String(float) renders it: its fixed char[33] + dtostrf overflow
    // near FLT_MAX (stack smash). Clamp to +/-1e9 (<=10 digits) and drop non-finite values.
    static inline float displaySafeFloat(float v)
    {
        if (!std::isfinite(v))
            return 0.0f;
        return v < -1e9f ? -1e9f : (v > 1e9f ? 1e9f : v);
    }
};
