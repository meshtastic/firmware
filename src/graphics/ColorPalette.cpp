#include "configuration.h"
#if HAS_SCREEN

#include "ColorPalette.h"

namespace graphics
{

namespace
{
static uint16_t g_accent565 = COLOR565(255, 255, 128);
} // namespace

void setUIPaletteAccent(uint16_t accent565)
{
    g_accent565 = accent565;
}

uint16_t getUIPaletteAccent()
{
    return g_accent565;
}

void fillUIPalette565(uint16_t *palette, size_t paletteSize)
{
    if (palette == nullptr || paletteSize == 0) {
        return;
    }

    for (size_t i = 0; i < paletteSize; ++i) {
        palette[i] = COLOR565(0, 0, 0);
    }

    palette[kUIPaletteBackground] = COLOR565(0, 0, 0);
    palette[kUIPaletteForeground] = COLOR565(240, 245, 250);
    palette[kUIPaletteAccent] = g_accent565;
    palette[kUIPaletteSuccess] = COLOR565(85, 220, 120);
    palette[kUIPaletteWarning] = COLOR565(255, 200, 70);
    palette[kUIPaletteError] = COLOR565(255, 90, 90);
    palette[kUIPaletteDisabled] = COLOR565(120, 130, 140);
    palette[kUIPalettePanelBorder] = COLOR565(170, 185, 200);
    palette[kUIPalettePanelFill] = COLOR565(24, 34, 44);
    palette[kUIPaletteInfo] = COLOR565(105, 190, 255);
    palette[kUIPaletteHighlight] = COLOR565(55, 120, 170);

    palette[kUIPaletteWeatherSun] = COLOR565(255, 210, 60);
    palette[kUIPaletteWeatherRain] = COLOR565(70, 175, 255);
    palette[kUIPaletteWeatherCloud] = COLOR565(150, 170, 185);
    palette[kUIPaletteWeatherSnow] = COLOR565(190, 240, 255);
    palette[kUIPaletteWeatherWind] = COLOR565(120, 240, 255);
    // kUIPaletteWeatherStorm/kUIPaletteWeatherTemp/kUIPaletteWeatherMoon reuse existing slots.
}

uint8_t mapWeatherColor565ToPaletteIndex(uint16_t color565)
{
    if (color565 == COLOR565(255, 210, 60)) {
        return kUIPaletteWeatherSun;
    }
    if (color565 == COLOR565(70, 175, 255)) {
        return kUIPaletteWeatherRain;
    }
    if (color565 == COLOR565(150, 170, 185)) {
        return kUIPaletteWeatherCloud;
    }
    if (color565 == COLOR565(190, 240, 255)) {
        return kUIPaletteWeatherSnow;
    }
    if (color565 == COLOR565(120, 240, 255)) {
        return kUIPaletteWeatherWind;
    }
    if (color565 == COLOR565(255, 235, 70)) {
        return kUIPaletteWeatherStorm;
    }
    if (color565 == COLOR565(255, 110, 60)) {
        return kUIPaletteWeatherTemp;
    }
    if (color565 == COLOR565(210, 225, 255)) {
        return kUIPaletteWeatherMoon;
    }

    return kUIPaletteAccent;
}

} // namespace graphics

#endif
