#pragma once

#include <algorithm>
#include <cstdint>

namespace NicheGraphics::InkHUD
{
struct ReaderToolbar {
    static constexpr uint16_t height = 128;
    static constexpr uint8_t count = 5;
    static constexpr uint16_t progressBottomInset = 8;
    static uint16_t progressFooterHeight(uint16_t lineHeight) { return lineHeight + 2U * progressBottomInset; }

    static uint16_t inset(uint16_t width) { return std::min<uint16_t>(24, width / 12); }
    static uint16_t boundary(uint16_t width, uint8_t index)
    {
        return inset(width) + static_cast<uint32_t>(width - 2 * inset(width)) * index / count;
    }
    static uint8_t hit(uint16_t width, uint16_t x)
    {
        for (uint8_t i = 1; i < count; ++i) {
            if (x < boundary(width, i))
                return i - 1;
        }
        return count - 1;
    }
};
} // namespace NicheGraphics::InkHUD
