#include "UnitConversions.h"

float UnitConversions::CelsiusToFahrenheit(float celsius)
{
    return (celsius * 9) / 5 + 32;
}

float UnitConversions::MetersPerSecondToKnots(float metersPerSecond)
{
    return metersPerSecond * 1.94384;
}

float UnitConversions::MetersPerSecondToMilesPerHour(float metersPerSecond)
{
    return metersPerSecond * 2.23694;
}

float UnitConversions::HectoPascalToInchesOfMercury(float hectoPascal)
{
    return hectoPascal * 0.029529983071445;
}
