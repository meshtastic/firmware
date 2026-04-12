#include "TFTColorRegions.h"
#include "TFTPalette.h"

#include <string.h>

namespace graphics
{
TFTColorRegion colorRegions[MAX_TFT_COLOR_REGIONS];

namespace
{

struct TFTRoleColorsBe {
    uint16_t onColorBe;
    uint16_t offColorBe;
};

static uint8_t colorRegionCount = 0;

static constexpr uint16_t toBe565(uint16_t color)
{
    return static_cast<uint16_t>((color >> 8) | (color << 8));
}

#ifdef TFT_HEADER_BG_COLOR_OVERRIDE
static constexpr uint16_t kHeaderBackground = TFT_HEADER_BG_COLOR_OVERRIDE;
#else
static constexpr uint16_t kHeaderBackground = TFTPalette::DarkGray;
#endif

#ifdef TFT_HEADER_TITLE_COLOR_OVERRIDE
static constexpr uint16_t kTitleColor = TFT_HEADER_TITLE_COLOR_OVERRIDE;
#else
static constexpr uint16_t kTitleColor = TFTPalette::White;
#endif

#ifdef TFT_HEADER_STATUS_COLOR_OVERRIDE
static constexpr uint16_t kStatusColor = TFT_HEADER_STATUS_COLOR_OVERRIDE;
#else
static constexpr uint16_t kStatusColor = TFTPalette::White;
#endif

static TFTRoleColorsBe roleColors[static_cast<size_t>(TFTColorRole::Count)] = {
    {toBe565(kHeaderBackground), toBe565(TFTPalette::Black)},    // HeaderBackground
    {toBe565(kHeaderBackground), toBe565(kTitleColor)},          // HeaderTitle
    {toBe565(kHeaderBackground), toBe565(kStatusColor)},         // HeaderStatus
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // SignalBars
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // BatteryFill
    {toBe565(TFTPalette::Blue), toBe565(TFTPalette::Black)},     // ConnectionIcon
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // ChannelUtilization
    {toBe565(TFTPalette::Yellow), toBe565(TFTPalette::Black)},   // FavoriteNode
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::Black)}, // ActionMenuBorder
    {toBe565(TFTPalette::White), toBe565(TFTPalette::Black)},    // ActionMenuBody
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::White)}  // ActionMenuTitle
};

} // namespace

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor)
{
    if (!isTFTColoringEnabled()) {
        return;
    }

    const uint8_t index = static_cast<uint8_t>(role);
    if (index >= static_cast<uint8_t>(TFTColorRole::Count)) {
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

    if (width <= 0 || height <= 0) {
        return;
    }

    const uint8_t roleIndex = static_cast<uint8_t>(role);
    if (roleIndex >= static_cast<uint8_t>(TFTColorRole::Count)) {
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
    colorRegions[colorRegionCount++] = {x, y, width, height, colors.onColorBe, colors.offColorBe, true};
    // Keep a disabled terminator after active regions.
    if (colorRegionCount < MAX_TFT_COLOR_REGIONS) {
        colorRegions[colorRegionCount].enabled = false;
    }
}

void clearTFTColorRegions()
{
    // Clear enabled flags so external drivers don't reuse stale regions.
    for (uint8_t i = 0; i < colorRegionCount; i++) {
        colorRegions[i].enabled = false;
    }
    if (colorRegionCount < MAX_TFT_COLOR_REGIONS) {
        colorRegions[colorRegionCount].enabled = false;
    }
    colorRegionCount = 0;
}

uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool isset, uint16_t defaultOnColor, uint16_t defaultOffColor)
{
    // Walk registered color regions in reverse order so later (higher-priority) regions win
    for (int i = static_cast<int>(colorRegionCount) - 1; i >= 0; i--) {
        const TFTColorRegion &r = colorRegions[i];
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            return isset ? r.onColorBe : r.offColorBe;
        }
    }
    return isset ? defaultOnColor : defaultOffColor;
}

} // namespace graphics
