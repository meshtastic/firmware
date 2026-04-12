#pragma once

#include "configuration.h"
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

#if HAS_TFT || defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) ||                 \
    defined(ST7789_CS) || defined(HX8357_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(ST7796_CS) ||              \
    defined(USE_ST7796) || defined(HACKADAY_COMMUNICATOR)
#define GRAPHICS_TFT_COLORING_ENABLED 1
#else
#define GRAPHICS_TFT_COLORING_ENABLED 0
#endif

static constexpr bool kTFTColoringEnabled = GRAPHICS_TFT_COLORING_ENABLED != 0;
constexpr bool isTFTColoringEnabled()
{
    return kTFTColoringEnabled;
}

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor);
void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height);
void clearTFTColorRegions();
uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool isset, uint16_t defaultOnColor, uint16_t defaultOffColor);

} // namespace graphics
