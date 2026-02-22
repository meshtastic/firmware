#include "TFTColorRegions.h"
#include "TFTPalette.h"

#include "configuration.h"
#include <OLEDDisplay.h>
#include <string.h>

namespace graphics
{

namespace
{

struct TFTRoleColorsBe {
    uint16_t onColorBe;
    uint16_t offColorBe;
};

struct TFTColorRegion {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint16_t onColorBe;
    uint16_t offColorBe;
};

static constexpr size_t MAX_TFT_COLOR_REGIONS = 48;
static TFTColorRegion colorRegions[MAX_TFT_COLOR_REGIONS];
static size_t colorRegionCount = 0;
static TFTRoleColorsBe roleColors[static_cast<size_t>(TFTColorRole::Count)];
static bool roleColorsInitialized = false;

static uint16_t toBe565(uint16_t color)
{
    return static_cast<uint16_t>((color >> 8) | (color << 8));
}

static void initializeRoleColors()
{
    if (roleColorsInitialized) {
        return;
    }

#ifdef TFT_HEADER_BG_COLOR_OVERRIDE
    const uint16_t headerBackground = TFT_HEADER_BG_COLOR_OVERRIDE;
#else
    const uint16_t headerBackground = TFTPalette::DarkGray;
#endif

#ifdef TFT_HEADER_TITLE_COLOR_OVERRIDE
    const uint16_t titleColor = TFT_HEADER_TITLE_COLOR_OVERRIDE;
#else
    const uint16_t titleColor = TFTPalette::White;
#endif

#ifdef TFT_HEADER_STATUS_COLOR_OVERRIDE
    const uint16_t statusColor = TFT_HEADER_STATUS_COLOR_OVERRIDE;
#else
    const uint16_t statusColor = TFTPalette::White;
#endif

    const uint16_t signalBarsColor = TFTPalette::Good;
    const uint16_t batteryFillColor = TFTPalette::Good;
    const uint16_t connectionIconColor = TFTPalette::Blue;
    const uint16_t channelUtilizationColor = TFTPalette::Good;
    const uint16_t favoriteNodeColor = TFTPalette::Yellow;
    const uint16_t actionMenuBorderColor = TFTPalette::DarkGray;
    const uint16_t actionMenuTitleBackgroundColor = TFTPalette::DarkGray;
    const uint16_t actionMenuTitleTextColor = TFTPalette::White;

    roleColors[static_cast<size_t>(TFTColorRole::HeaderBackground)].onColorBe = toBe565(headerBackground);
    roleColors[static_cast<size_t>(TFTColorRole::HeaderBackground)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::HeaderTitle)].onColorBe = toBe565(headerBackground);
    roleColors[static_cast<size_t>(TFTColorRole::HeaderTitle)].offColorBe = toBe565(titleColor);
    roleColors[static_cast<size_t>(TFTColorRole::HeaderStatus)].onColorBe = toBe565(headerBackground);
    roleColors[static_cast<size_t>(TFTColorRole::HeaderStatus)].offColorBe = toBe565(statusColor);
    roleColors[static_cast<size_t>(TFTColorRole::SignalBars)].onColorBe = toBe565(signalBarsColor);
    roleColors[static_cast<size_t>(TFTColorRole::SignalBars)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::BatteryFill)].onColorBe = toBe565(batteryFillColor);
    roleColors[static_cast<size_t>(TFTColorRole::BatteryFill)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::ConnectionIcon)].onColorBe = toBe565(connectionIconColor);
    roleColors[static_cast<size_t>(TFTColorRole::ConnectionIcon)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::ChannelUtilization)].onColorBe = toBe565(channelUtilizationColor);
    roleColors[static_cast<size_t>(TFTColorRole::ChannelUtilization)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::FavoriteNode)].onColorBe = toBe565(favoriteNodeColor);
    roleColors[static_cast<size_t>(TFTColorRole::FavoriteNode)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::ActionMenuBorder)].onColorBe = toBe565(actionMenuBorderColor);
    roleColors[static_cast<size_t>(TFTColorRole::ActionMenuBorder)].offColorBe = toBe565(TFTPalette::Black);
    roleColors[static_cast<size_t>(TFTColorRole::ActionMenuTitle)].onColorBe = toBe565(actionMenuTitleBackgroundColor);
    roleColors[static_cast<size_t>(TFTColorRole::ActionMenuTitle)].offColorBe = toBe565(actionMenuTitleTextColor);
    roleColorsInitialized = true;
}

} // namespace

bool isTFTColoringEnabled()
{
#if HAS_TFT || defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) ||                 \
    defined(ST7789_CS) || defined(HX8357_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(ST7796_CS) ||              \
    defined(USE_ST7796) || defined(HACKADAY_COMMUNICATOR)
    return true;
#else
    return false;
#endif
}

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor)
{
    if (!isTFTColoringEnabled()) {
        return;
    }

    initializeRoleColors();

    const size_t index = static_cast<size_t>(role);
    if (index >= static_cast<size_t>(TFTColorRole::Count)) {
        return;
    }

    roleColors[index].onColorBe = toBe565(onColor);
    roleColors[index].offColorBe = toBe565(offColor);
}

void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height)
{
    if (!isTFTColoringEnabled()) {
        return;
    }

    initializeRoleColors();

    if (width <= 0 || height <= 0) {
        return;
    }

    const size_t roleIndex = static_cast<size_t>(role);
    if (roleIndex >= static_cast<size_t>(TFTColorRole::Count)) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    if (colorRegionCount >= MAX_TFT_COLOR_REGIONS) {
        memmove(&colorRegions[0], &colorRegions[1], sizeof(TFTColorRegion) * (MAX_TFT_COLOR_REGIONS - 1));
        colorRegionCount = MAX_TFT_COLOR_REGIONS - 1;
    }

    const TFTRoleColorsBe &colors = roleColors[roleIndex];
    colorRegions[colorRegionCount++] = {x, y, width, height, colors.onColorBe, colors.offColorBe};
}

uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool pixelSet, uint16_t fallbackOnColorBe, uint16_t fallbackOffColorBe)
{
    if (!isTFTColoringEnabled()) {
        return pixelSet ? fallbackOnColorBe : fallbackOffColorBe;
    }

    initializeRoleColors();

    for (int32_t i = static_cast<int32_t>(colorRegionCount) - 1; i >= 0; --i) {
        const TFTColorRegion &region = colorRegions[i];
        if (x >= region.x && x < region.x + region.width && y >= region.y && y < region.y + region.height) {
            return pixelSet ? region.onColorBe : region.offColorBe;
        }
    }

    return pixelSet ? fallbackOnColorBe : fallbackOffColorBe;
}

void clearTFTColorRegions()
{
    colorRegionCount = 0;
}

} // namespace graphics
