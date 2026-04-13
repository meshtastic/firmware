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
static constexpr uint32_t kFnv1aOffsetBasis = 2166136261u;
static constexpr uint32_t kFnv1aPrime = 16777619u;

static constexpr uint16_t toBe565(uint16_t color)
{
    return static_cast<uint16_t>((color >> 8) | (color << 8));
}

static inline uint32_t fnv1aAppendByte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * kFnv1aPrime;
}

static inline uint32_t fnv1aAppendU16(uint32_t hash, uint16_t value)
{
    hash = fnv1aAppendByte(hash, static_cast<uint8_t>(value & 0xFF));
    hash = fnv1aAppendByte(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
    return hash;
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
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

    const uint8_t index = static_cast<uint8_t>(role);
    if (index >= static_cast<uint8_t>(TFTColorRole::Count)) {
        return;
    }

    roleColors[index].onColorBe = toBe565(onColor);
    roleColors[index].offColorBe = toBe565(offColor);
}

void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

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

void registerTFTActionMenuRegions(int16_t boxLeft, int16_t boxTop, int16_t boxWidth, int16_t boxHeight)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    (void)boxLeft;
    (void)boxTop;
    (void)boxWidth;
    (void)boxHeight;
    return;
#else
    // Fill role includes a 1px shadow guard so stale frame edges are overwritten uniformly.
    setTFTColorRole(TFTColorRole::ActionMenuBody, TFTPalette::White, TFTPalette::Black);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop - 2, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft - 2, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft + boxWidth + 1, boxTop, 1, boxHeight);

    setTFTColorRole(TFTColorRole::ActionMenuBorder, TFTPalette::DarkGray, TFTPalette::Black);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop + boxHeight - 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft + boxWidth - 1, boxTop, 1, boxHeight);
#endif
}

uint32_t getTFTColorFrameSignature()
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return 0;
#else
    uint32_t hash = kFnv1aOffsetBasis;
    // Regions already store resolved on/off colors, so hashing the active region list
    // is enough to detect visible color-state changes for this frame.
    hash = fnv1aAppendByte(hash, colorRegionCount);
    for (uint8_t i = 0; i < colorRegionCount; i++) {
        const TFTColorRegion &r = colorRegions[i];
        hash = fnv1aAppendU16(hash, static_cast<uint16_t>(r.x));
        hash = fnv1aAppendU16(hash, static_cast<uint16_t>(r.y));
        hash = fnv1aAppendU16(hash, static_cast<uint16_t>(r.width));
        hash = fnv1aAppendU16(hash, static_cast<uint16_t>(r.height));
        hash = fnv1aAppendU16(hash, r.onColorBe);
        hash = fnv1aAppendU16(hash, r.offColorBe);
    }

    return hash;
#endif
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
