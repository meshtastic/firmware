#pragma once

#include <stdint.h>

namespace graphics
{

struct TFTColorRegion {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint16_t onColorBe;
    uint16_t offColorBe;
    bool enabled = false;
};

static constexpr size_t MAX_TFT_COLOR_REGIONS = 48;
extern TFTColorRegion colorRegions[MAX_TFT_COLOR_REGIONS];

enum class TFTColorRole : uint8_t {
    HeaderBackground = 0,
    HeaderTitle,
    HeaderStatus,
    SignalBars,
    BatteryFill,
    ConnectionIcon,
    ChannelUtilization,
    FavoriteNode,
    ActionMenuBorder,
    ActionMenuTitle,
    Count
};

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor);
void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height);
void clearTFTColorRegions();
bool isTFTColoringEnabled();
uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool isset, uint16_t defaultOnColor, uint16_t defaultOffColor);

} // namespace graphics
