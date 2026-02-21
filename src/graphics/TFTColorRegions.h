#pragma once

#include <stdint.h>

namespace graphics
{

enum class TFTColorRole : uint8_t {
    HeaderBackground = 0,
    HeaderTitle,
    HeaderStatus,
    SignalBars,
    BatteryFill,
    ConnectionIcon,
    ChannelUtilization,
    FavoriteNode,
    Count
};

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor);
void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height);
uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool pixelSet, uint16_t fallbackOnColorBe, uint16_t fallbackOffColorBe);
void clearTFTColorRegions();
bool isTFTColoringEnabled();

} // namespace graphics
