#pragma once

#include "Applet.h"

namespace NicheGraphics::InkHUD
{
inline constexpr uint8_t ICON_NATIVE_SIZE = 48;
using IconBitmap = const uint64_t *;

inline void drawIconBitmapScaled(Applet *applet, IconBitmap bitmap, int16_t left, int16_t top, uint16_t size, uint16_t color)
{
    if (!bitmap || size == 0)
        return;

    for (uint16_t y = 0; y < size; ++y) {
        const uint64_t row = bitmap[y * ICON_NATIVE_SIZE / size];
        for (uint16_t x = 0; x < size; ++x) {
            const uint16_t column = x * ICON_NATIVE_SIZE / size;
            if (row & (1ULL << (ICON_NATIVE_SIZE - 1 - column)))
                applet->fillRect(left + x, top + y, 1, 1, color);
        }
    }
}
} // namespace NicheGraphics::InkHUD
