#pragma once

#include <Arduino.h>

namespace graphics
{

enum class ColorOverlayType : uint8_t {
    Xbm,
    Rect,
};

struct ColorOverlay {
    ColorOverlayType type;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    const uint8_t *xbm;
    uint8_t paletteIndex;
    int16_t clipLeft;
    int16_t clipTop;
    int16_t clipRight;
    int16_t clipBottom;
};

void setColorOverlayClip(int16_t left, int16_t top, int16_t right, int16_t bottom);
void clearColorOverlays();
void queueColorOverlayXbm(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm, uint8_t paletteIndex);
void queueColorOverlayRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t paletteIndex);

const ColorOverlay *getCurrentColorOverlays(uint8_t &count);
const ColorOverlay *getPreviousColorOverlays(uint8_t &count);
void finishColorOverlayFrame();

} // namespace graphics

