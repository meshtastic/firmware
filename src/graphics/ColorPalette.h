#pragma once

#include <Arduino.h>

namespace graphics
{

enum UIPaletteIndex : uint8_t {
    kUIPaletteBackground = 0,
    kUIPaletteForeground = 1,
    kUIPaletteAccent = 2,
    kUIPaletteSuccess = 3,
    kUIPaletteWarning = 4,
    kUIPaletteError = 5,
    kUIPaletteDisabled = 6,
    kUIPalettePanelBorder = 7,
    kUIPalettePanelFill = 8,
    kUIPaletteInfo = 9,
    kUIPaletteHighlight = 10,

    // 16-color indexed mode for T114 uses 0..15.
    kUIPaletteWeatherSun = 11,
    kUIPaletteWeatherRain = 12,
    kUIPaletteWeatherCloud = 13,
    kUIPaletteWeatherSnow = 14,
    kUIPaletteWeatherWind = 15,
    // Reuse close visual roles to stay within 16 slots.
    kUIPaletteWeatherStorm = kUIPaletteWarning,
    kUIPaletteWeatherTemp = kUIPaletteError,
    kUIPaletteWeatherMoon = kUIPaletteInfo,
};

void setUIPaletteAccent(uint16_t accent565);
uint16_t getUIPaletteAccent();
void fillUIPalette565(uint16_t *palette, size_t paletteSize);
uint8_t mapWeatherColor565ToPaletteIndex(uint16_t color565);

} // namespace graphics
