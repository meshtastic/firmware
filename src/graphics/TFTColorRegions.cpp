#include "TFTColorRegions.h"
#include "NodeDB.h"
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

static constexpr bool kRoleIsBody[static_cast<size_t>(TFTColorRole::Count)] = {
    false, // HeaderBackground
    false, // HeaderTitle
    false, // HeaderStatus
    true,  // SignalBars
    true,  // ConnectionIcon
    true,  // UtilizationFill
    true,  // FavoriteNode
    true,  // ActionMenuBorder
    true,  // ActionMenuBody
    true,  // ActionMenuTitle
    true,  // FrameMono
    false, // BootSplash
    true,  // FavoriteNodeBGHighlight
    false, // NavigationBar
    false  // NavigationArrow
};

static inline bool isBodyColorRole(TFTColorRole role)
{
    return kRoleIsBody[static_cast<size_t>(role)];
}

static inline bool isMonochromeTheme(uint32_t themeId)
{
    return themeId == ThemeID::MeshtasticGreen || themeId == ThemeID::ClassicRed || themeId == ThemeID::MonochromeWhite;
}

static inline uint16_t getMonochromeAccent(uint32_t themeId)
{
    return (themeId == ThemeID::MeshtasticGreen) ? TFTPalette::MeshtasticGreen
           : (themeId == ThemeID::ClassicRed)    ? TFTPalette::ClassicRed
                                                 : TFTPalette::White;
}

static inline void replaceColor(uint16_t &value, uint16_t from, uint16_t to)
{
    if (value == from) {
        value = to;
    }
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

// Compile-time header color overrides (backward-compatible)
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

// Theme definitions
// Stored in kThemes[] and looked up by matching uiconfig.screen_rgb_color
// against each entry's .uniqueIdentifier field.

static const TFTThemeDef kThemes[] = {

    // Default Dark (ThemeID::DefaultDark = 0)
    {
        ThemeID::DefaultDark, // id
        "Default Dark",       // name
        0,                    // uniqueIdentifier
        // roles[TFTColorRole::Count]
        {
            {kHeaderBackground, TFTPalette::Black},    // HeaderBackground
            {kHeaderBackground, kTitleColor},          // HeaderTitle
            {kHeaderBackground, kStatusColor},         // HeaderStatus
            {TFTPalette::Good, TFTPalette::Black},     // SignalBars
            {TFTPalette::Blue, TFTPalette::Black},     // ConnectionIcon
            {TFTPalette::Good, TFTPalette::Black},     // UtilizationFill
            {TFTPalette::Yellow, TFTPalette::Black},   // FavoriteNode
            {TFTPalette::DarkGray, TFTPalette::Black}, // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Black},    // ActionMenuBody
            {TFTPalette::DarkGray, TFTPalette::White}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},    // FrameMono
            {TFTPalette::White, TFTPalette::Black},    // BootSplash
            {TFTPalette::Yellow, TFTPalette::Black},   // FavoriteNodeBGHighlight
            {kStatusColor, kHeaderBackground},         // NavigationBar  (icon fg, bar bg)
            {kTitleColor, TFTPalette::Black},          // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::Good,   // batteryFillGood
        TFTPalette::Medium, // batteryFillMedium
        TFTPalette::Bad,    // batteryFillBad
        false,              // fullFrameInvert
        true,               // visible
    },

    // Default Light (ThemeID::DefaultLight = 1)
    {
        ThemeID::DefaultLight, // id
        "Default Light",       // name
        1,                     // uniqueIdentifier
        {
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderTitle
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderStatus
            {TFTPalette::Good, TFTPalette::White},      // SignalBars
            {TFTPalette::Blue, TFTPalette::White},      // ConnectionIcon
            {TFTPalette::Good, TFTPalette::White},      // UtilizationFill
            {TFTPalette::Black, TFTPalette::Yellow},    // FavoriteNode
            {TFTPalette::DarkGray, TFTPalette::White},  // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::White},     // ActionMenuBody
            {TFTPalette::DarkGray, TFTPalette::Black},  // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},     // FrameMono
            {TFTPalette::White, TFTPalette::Black},     // BootSplash
            {TFTPalette::Black, TFTPalette::Yellow},    // FavoriteNodeBGHighlight
            {TFTPalette::Black, TFTPalette::LightGray}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::Black, TFTPalette::White},     // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::Good,   // batteryFillGood
        TFTPalette::Medium, // batteryFillMedium
        TFTPalette::Bad,    // batteryFillBad
        true,               // fullFrameInvert
        true,               // visible
    },

    // Christmas (ThemeID::Christmas = 2)
    {
        ThemeID::Christmas, // id
        "Christmas",        // name
        2,                  // uniqueIdentifier
        {
            {TFTPalette::ChristmasRed, TFTPalette::Black},  // HeaderBackground
            {TFTPalette::ChristmasRed, TFTPalette::Gold},   // HeaderTitle
            {TFTPalette::ChristmasRed, TFTPalette::Gold},   // HeaderStatus
            {TFTPalette::ChristmasGreen, TFTPalette::Pine}, // SignalBars
            {TFTPalette::Gold, TFTPalette::Pine},           // ConnectionIcon
            {TFTPalette::ChristmasGreen, TFTPalette::Pine}, // UtilizationFill
            {TFTPalette::Gold, TFTPalette::Pine},           // FavoriteNode
            {TFTPalette::ChristmasRed, TFTPalette::Pine},   // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Pine},          // ActionMenuBody
            {TFTPalette::ChristmasRed, TFTPalette::White},  // ActionMenuTitle
            {TFTPalette::Pine, TFTPalette::White},          // FrameMono
            {TFTPalette::White, TFTPalette::ChristmasRed},  // BootSplash
            {TFTPalette::Gold, TFTPalette::Pine},           // FavoriteNodeBGHighlight
            {TFTPalette::Gold, TFTPalette::ChristmasRed},   // NavigationBar  (icon fg, bar bg)
            {TFTPalette::Gold, TFTPalette::Pine},           // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::ChristmasGreen, // batteryFillGood
        TFTPalette::Gold,           // batteryFillMedium
        TFTPalette::ChristmasRed,   // batteryFillBad
        true,                       // fullFrameInvert
        false,                      // visible
    },

    // Pink (ThemeID::Pink = 3) light variant
    {
        ThemeID::Pink, // id
        "Pink",        // name
        3,             // uniqueIdentifier
        {
            {TFTPalette::HotPink, TFTPalette::Black},     // HeaderBackground
            {TFTPalette::HotPink, TFTPalette::White},     // HeaderTitle
            {TFTPalette::HotPink, TFTPalette::White},     // HeaderStatus
            {TFTPalette::DeepPink, TFTPalette::PalePink}, // SignalBars
            {TFTPalette::HotPink, TFTPalette::PalePink},  // ConnectionIcon
            {TFTPalette::DeepPink, TFTPalette::PalePink}, // UtilizationFill
            {TFTPalette::Black, TFTPalette::HotPink},     // FavoriteNode
            {TFTPalette::HotPink, TFTPalette::PalePink},  // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::PalePink},    // ActionMenuBody
            {TFTPalette::HotPink, TFTPalette::White},     // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::HotPink},     // BootSplash
            {TFTPalette::Black, TFTPalette::HotPink},     // FavoriteNodeBGHighlight
            {TFTPalette::White, TFTPalette::HotPink},     // NavigationBar  (icon fg, bar bg)
            {TFTPalette::HotPink, TFTPalette::PalePink},  // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::DeepPink, // batteryFillGood
        TFTPalette::HotPink,  // batteryFillMedium
        TFTPalette::Bad,      // batteryFillBad
        true,                 // fullFrameInvert
        true,                 // visible
    },

    // Blue (ThemeID::Blue = 4) dark variant
    {
        ThemeID::Blue, // id
        "Blue",        // name
        4,             // uniqueIdentifier
        {
            {TFTPalette::DeepBlue, TFTPalette::Black},   // HeaderBackground
            {TFTPalette::DeepBlue, TFTPalette::White},   // HeaderTitle
            {TFTPalette::DeepBlue, TFTPalette::SkyBlue}, // HeaderStatus
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // SignalBars
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // ConnectionIcon
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // UtilizationFill
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // FavoriteNode
            {TFTPalette::DeepBlue, TFTPalette::Navy},    // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Navy},       // ActionMenuBody
            {TFTPalette::DeepBlue, TFTPalette::White},   // ActionMenuTitle
            {TFTPalette::Navy, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::DeepBlue},   // BootSplash
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // FavoriteNodeBGHighlight
            {TFTPalette::SkyBlue, TFTPalette::DeepBlue}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::SkyBlue, TFTPalette::Black},    // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::SkyBlue, // batteryFillGood
        TFTPalette::Medium,  // batteryFillMedium
        TFTPalette::Bad,     // batteryFillBad
        true,                // fullFrameInvert
        true,                // visible
    },

    // Creamsicle (ThemeID::Creamsicle = 5)light variant
    {
        ThemeID::Creamsicle, // id
        "Creamsicle",        // name
        5,                   // uniqueIdentifier
        {
            {TFTPalette::CreamOrange, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::CreamOrange, TFTPalette::White}, // HeaderTitle
            {TFTPalette::CreamOrange, TFTPalette::White}, // HeaderStatus
            {TFTPalette::DeepOrange, TFTPalette::Cream},  // SignalBars
            {TFTPalette::CreamOrange, TFTPalette::Cream}, // ConnectionIcon
            {TFTPalette::DeepOrange, TFTPalette::Cream},  // UtilizationFill
            {TFTPalette::Black, TFTPalette::CreamOrange}, // FavoriteNode
            {TFTPalette::CreamOrange, TFTPalette::Cream}, // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::Cream},       // ActionMenuBody
            {TFTPalette::CreamOrange, TFTPalette::White}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::CreamOrange}, // BootSplash
            {TFTPalette::Black, TFTPalette::CreamOrange}, // FavoriteNodeBGHighlight
            {TFTPalette::White, TFTPalette::CreamOrange}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::CreamOrange, TFTPalette::White}, // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::DeepOrange, // batteryFillGood
        TFTPalette::Gold,       // batteryFillMedium
        TFTPalette::Bad,        // batteryFillBad
        true,                   // fullFrameInvert
        true,                   // visible
    },

    // Meshtastic Green (ThemeID::MeshtasticGreen = 6) classic monochrome
    // Pure single-color-on-black look.  Every role maps foreground pixels to
    // the theme color and background pixels to Black.
    {
        ThemeID::MeshtasticGreen, // id
        "Meshtastic Green",       // name
        6,                        // uniqueIdentifier
        {
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // HeaderTitle
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // HeaderStatus
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // SignalBars
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // ConnectionIcon
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // UtilizationFill
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // FavoriteNode
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // ActionMenuBorder
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // ActionMenuBody
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::MeshtasticGreen}, // FrameMono (bodyBg, bodyFg)
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // BootSplash
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // FavoriteNodeBGHighlight
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // NavigationBar
            {TFTPalette::MeshtasticGreen, TFTPalette::Black}, // NavigationArrow
        },
        TFTPalette::Black, // batteryFillGood
        TFTPalette::Black, // batteryFillMedium
        TFTPalette::Black, // batteryFillBad
        true,              // fullFrameInvert
        true,              // visible
    },

    // Classic Red (ThemeID::ClassicRed = 7) classic monochrome
    {
        ThemeID::ClassicRed, // id
        "Classic Red",       // name
        7,                   // uniqueIdentifier
        {
            {TFTPalette::ClassicRed, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::ClassicRed, TFTPalette::Black}, // HeaderTitle
            {TFTPalette::ClassicRed, TFTPalette::Black}, // HeaderStatus
            {TFTPalette::ClassicRed, TFTPalette::Black}, // SignalBars
            {TFTPalette::ClassicRed, TFTPalette::Black}, // ConnectionIcon
            {TFTPalette::ClassicRed, TFTPalette::Black}, // UtilizationFill
            {TFTPalette::ClassicRed, TFTPalette::Black}, // FavoriteNode
            {TFTPalette::ClassicRed, TFTPalette::Black}, // ActionMenuBorder
            {TFTPalette::ClassicRed, TFTPalette::Black}, // ActionMenuBody
            {TFTPalette::ClassicRed, TFTPalette::Black}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::ClassicRed}, // FrameMono (bodyBg, bodyFg)
            {TFTPalette::ClassicRed, TFTPalette::Black}, // BootSplash
            {TFTPalette::ClassicRed, TFTPalette::Black}, // FavoriteNodeBGHighlight
            {TFTPalette::ClassicRed, TFTPalette::Black}, // NavigationBar
            {TFTPalette::ClassicRed, TFTPalette::Black}, // NavigationArrow
        },
        TFTPalette::Black, // batteryFillGood
        TFTPalette::Black, // batteryFillMedium
        TFTPalette::Black, // batteryFillBad
        true,              // fullFrameInvert
        true,              // visible
    },

    // Monochrome White (ThemeID::MonochromeWhite = 8) classic monochrome
    {
        ThemeID::MonochromeWhite, // id
        "Monochrome White",       // name
        8,                        // uniqueIdentifier
        {
            {TFTPalette::White, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::White, TFTPalette::Black}, // HeaderTitle
            {TFTPalette::White, TFTPalette::Black}, // HeaderStatus
            {TFTPalette::White, TFTPalette::Black}, // SignalBars
            {TFTPalette::White, TFTPalette::Black}, // ConnectionIcon
            {TFTPalette::White, TFTPalette::Black}, // UtilizationFill
            {TFTPalette::White, TFTPalette::Black}, // FavoriteNode
            {TFTPalette::White, TFTPalette::Black}, // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Black}, // ActionMenuBody
            {TFTPalette::White, TFTPalette::Black}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White}, // FrameMono (bodyBg, bodyFg)
            {TFTPalette::White, TFTPalette::Black}, // BootSplash
            {TFTPalette::White, TFTPalette::Black}, // FavoriteNodeBGHighlight
            {TFTPalette::White, TFTPalette::Black}, // NavigationBar
            {TFTPalette::White, TFTPalette::Black}, // NavigationArrow
        },
        TFTPalette::Black, // batteryFillGood
        TFTPalette::Black, // batteryFillMedium
        TFTPalette::Black, // batteryFillBad
        true,              // fullFrameInvert
        true,              // visible
    },
};

static constexpr size_t kInternalThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

// Resolve the kThemes[] index for the currently persisted theme.  Called at
// boot (indirectly via getActiveTheme()) and whenever the active theme is
// queried, so uiconfig.screen_rgb_color remains the single source of truth.
// Matches against .uniqueIdentifier - that's the field whose value is stored
// in the user's config.  Falls back to 0 (DefaultDark) if no match is found,
// which gracefully handles removed or retired themes.
static inline size_t resolveThemeIndex()
{
    const uint32_t savedIdentifier = uiconfig.screen_rgb_color & 0x1F;
    for (size_t i = 0; i < kInternalThemeCount; i++) {
        if (kThemes[i].uniqueIdentifier == savedIdentifier)
            return i;
    }
    return 0; // Default Dark fallback
}

static inline bool normalizeRegion(int16_t &x, int16_t &y, int16_t &width, int16_t &height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }

    return width > 0 && height > 0;
}

static inline void appendColorRegion(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t onColorBe, uint16_t offColorBe)
{
    // Keep the last slot permanently disabled as a sentinel for ST7789 scans.
    // This leaves MAX_TFT_COLOR_REGIONS - 1 usable entries.
    if (colorRegionCount >= MAX_TFT_COLOR_REGIONS - 1) {
        memmove(&colorRegions[0], &colorRegions[1], sizeof(TFTColorRegion) * (MAX_TFT_COLOR_REGIONS - 2));
        colorRegionCount = MAX_TFT_COLOR_REGIONS - 2;
    }

    TFTColorRegion &region = colorRegions[colorRegionCount++];
    region.x = x;
    region.y = y;
    region.width = width;
    region.height = height;
    region.onColorBe = onColorBe;
    region.offColorBe = offColorBe;
    region.enabled = true;

    // Keep one disabled sentinel after the active range for ST7789 countColorRegions().
    if (colorRegionCount < MAX_TFT_COLOR_REGIONS) {
        colorRegions[colorRegionCount].enabled = false;
    }
    colorRegions[MAX_TFT_COLOR_REGIONS - 1].enabled = false;
}

// Current working role colors (big-endian).  Initialised to Dark defaults;
// call loadThemeDefaults() after boot / theme change to refresh.
static TFTRoleColorsBe roleColors[static_cast<size_t>(TFTColorRole::Count)] = {
    {toBe565(kHeaderBackground), toBe565(TFTPalette::Black)},    // HeaderBackground
    {toBe565(kHeaderBackground), toBe565(kTitleColor)},          // HeaderTitle
    {toBe565(kHeaderBackground), toBe565(kStatusColor)},         // HeaderStatus
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // SignalBars
    {toBe565(TFTPalette::Blue), toBe565(TFTPalette::Black)},     // ConnectionIcon
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // UtilizationFill
    {toBe565(TFTPalette::Yellow), toBe565(TFTPalette::Black)},   // FavoriteNode
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::Black)}, // ActionMenuBorder
    {toBe565(TFTPalette::White), toBe565(TFTPalette::Black)},    // ActionMenuBody
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::White)}, // ActionMenuTitle
    {toBe565(TFTPalette::Black), toBe565(TFTPalette::White)},    // FrameMono
    {toBe565(TFTPalette::White), toBe565(TFTPalette::Black)},    // BootSplash
    {toBe565(TFTPalette::Yellow), toBe565(TFTPalette::Black)},   // FavoriteNodeBGHighlight
    {toBe565(kStatusColor), toBe565(kHeaderBackground)},         // NavigationBar
    {toBe565(kTitleColor), toBe565(TFTPalette::Black)}           // NavigationArrow
};

} // namespace

// Theme accessors

const TFTThemeDef &getActiveTheme()
{
    return kThemes[resolveThemeIndex()];
}

// Visible-theme accessors
// These iterate only themes flagged .visible = true, preserving kThemes[]
// order.  Menu code should use these so hidden themes don't appear in the
// picker while still applying correctly if their ID is persisted.

size_t getVisibleThemeCount()
{
    size_t count = 0;
    for (size_t i = 0; i < kInternalThemeCount; i++) {
        if (kThemes[i].visible)
            count++;
    }
    return count;
}

const TFTThemeDef &getVisibleThemeByIndex(size_t visibleIndex)
{
    size_t seen = 0;
    for (size_t i = 0; i < kInternalThemeCount; i++) {
        if (!kThemes[i].visible)
            continue;
        if (seen == visibleIndex)
            return kThemes[i];
        seen++;
    }
    // Fallback: return first theme (never trust a bad index).
    return kThemes[0];
}

size_t getActiveVisibleThemeIndex()
{
    const size_t active = resolveThemeIndex();
    if (!kThemes[active].visible)
        return SIZE_MAX;
    size_t visibleIdx = 0;
    for (size_t i = 0; i < active; i++) {
        if (kThemes[i].visible)
            visibleIdx++;
    }
    return visibleIdx;
}

uint16_t getThemeHeaderBg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
#ifdef TFT_HEADER_BG_COLOR_OVERRIDE
    return TFT_HEADER_BG_COLOR_OVERRIDE;
#else
    return kThemes[resolveThemeIndex()].roles[static_cast<size_t>(TFTColorRole::HeaderBackground)].onColor;
#endif
#else
    return TFTPalette::DarkGray;
#endif
}

uint16_t getThemeHeaderText()
{
#if GRAPHICS_TFT_COLORING_ENABLED
#ifdef TFT_HEADER_TITLE_COLOR_OVERRIDE
    return TFT_HEADER_TITLE_COLOR_OVERRIDE;
#else
    return kThemes[resolveThemeIndex()].roles[static_cast<size_t>(TFTColorRole::HeaderTitle)].offColor;
#endif
#else
    return TFTPalette::White;
#endif
}

uint16_t getThemeHeaderStatus()
{
#if GRAPHICS_TFT_COLORING_ENABLED
#ifdef TFT_HEADER_STATUS_COLOR_OVERRIDE
    return TFT_HEADER_STATUS_COLOR_OVERRIDE;
#else
    return kThemes[resolveThemeIndex()].roles[static_cast<size_t>(TFTColorRole::HeaderStatus)].offColor;
#endif
#else
    return TFTPalette::White;
#endif
}

uint16_t getThemeBodyBg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    return kThemes[resolveThemeIndex()].roles[static_cast<size_t>(TFTColorRole::FrameMono)].onColor;
#else
    return TFTPalette::Black;
#endif
}

uint16_t getThemeBodyFg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    return kThemes[resolveThemeIndex()].roles[static_cast<size_t>(TFTColorRole::FrameMono)].offColor;
#else
    return TFTPalette::White;
#endif
}

bool isThemeFullFrameInvert()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    return kThemes[resolveThemeIndex()].fullFrameInvert;
#else
    return false;
#endif
}

uint16_t getThemeBatteryFillColor(int batteryPercent)
{
    const TFTThemeDef &theme = kThemes[resolveThemeIndex()];
    if (batteryPercent <= 20) {
        return theme.batteryFillBad;
    }
    if (batteryPercent <= 50) {
        return theme.batteryFillMedium;
    }
    return theme.batteryFillGood;
}

void loadThemeDefaults()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    const TFTThemeDef &theme = kThemes[resolveThemeIndex()];
    for (uint8_t i = 0; i < static_cast<uint8_t>(TFTColorRole::Count); i++) {
        roleColors[i].onColorBe = toBe565(theme.roles[i].onColor);
        roleColors[i].offColorBe = toBe565(theme.roles[i].offColor);
    }
#endif
}

// Role color assignment with theme-aware transforms

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

    const uint8_t index = static_cast<uint8_t>(role);
    if (index >= static_cast<uint8_t>(TFTColorRole::Count)) {
        return;
    }

    const uint32_t themeId = uiconfig.screen_rgb_color & 0x1F;
    const bool isHighlightRole = (role == TFTColorRole::FavoriteNode || role == TFTColorRole::FavoriteNodeBGHighlight);
    const bool isBodyRole = !isHighlightRole && isBodyColorRole(role);

    // Classic monochrome themes collapse all non-black accents into one tone.
    if (isMonochromeTheme(themeId)) {
        if (onColor != TFTPalette::Black) {
            onColor = getMonochromeAccent(themeId);
        }
    } else {
        switch (themeId) {
        case ThemeID::DefaultLight:
            if (isHighlightRole) {
                // High-contrast highlight chips on light UI.
                onColor = TFTPalette::Black;
                offColor = TFTPalette::Yellow;
            } else if (isBodyRole) {
                // Invert body colors for readability on white frames.
                if (offColor == TFTPalette::Black && role != TFTColorRole::ActionMenuTitle) {
                    offColor = TFTPalette::White;
                }
                replaceColor(onColor, TFTPalette::White, TFTPalette::Black);
            }
            break;
        case ThemeID::Christmas:
            if (isHighlightRole || isBodyRole) {
                replaceColor(onColor, TFTPalette::Yellow, TFTPalette::Gold);
                replaceColor(offColor, TFTPalette::Black, TFTPalette::Pine);
            }
            break;
        case ThemeID::Pink:
            if (isHighlightRole) {
                onColor = TFTPalette::Black;
                offColor = TFTPalette::HotPink;
            } else if (isBodyRole) {
                replaceColor(offColor, TFTPalette::Black, TFTPalette::PalePink);
                replaceColor(onColor, TFTPalette::White, TFTPalette::Black);
                replaceColor(onColor, TFTPalette::Yellow, TFTPalette::DeepPink);
            }
            break;
        case ThemeID::Creamsicle:
            if (isHighlightRole) {
                onColor = TFTPalette::Black;
                offColor = TFTPalette::CreamOrange;
            } else if (isBodyRole) {
                replaceColor(offColor, TFTPalette::Black, TFTPalette::Cream);
                replaceColor(onColor, TFTPalette::White, TFTPalette::Black);
                replaceColor(onColor, TFTPalette::Yellow, TFTPalette::DeepOrange);
            }
            break;
        case ThemeID::Blue:
            if (isHighlightRole || isBodyRole) {
                replaceColor(onColor, TFTPalette::Yellow, TFTPalette::SkyBlue);
                replaceColor(offColor, TFTPalette::Black, TFTPalette::Navy);
            }
            break;
        default:
            break;
        }
    }

    roleColors[index].onColorBe = toBe565(onColor);
    roleColors[index].offColorBe = toBe565(offColor);
}

// Region registration

void registerTFTColorRegion(TFTColorRole role, int16_t x, int16_t y, int16_t width, int16_t height)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

    const uint8_t roleIndex = static_cast<uint8_t>(role);
    if (roleIndex >= static_cast<uint8_t>(TFTColorRole::Count)) {
        return;
    }

    if (!normalizeRegion(x, y, width, height)) {
        return;
    }

    const TFTRoleColorsBe &colors = roleColors[roleIndex];
    appendColorRegion(x, y, width, height, colors.onColorBe, colors.offColorBe);
}

void setAndRegisterTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor, int16_t x, int16_t y, int16_t width,
                                int16_t height)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    (void)role;
    (void)onColor;
    (void)offColor;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return;
#else
    setTFTColorRole(role, onColor, offColor);
    registerTFTColorRegion(role, x, y, width, height);
#endif
}

void registerTFTColorRegionDirect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t onColor, uint16_t offColor)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

    if (!normalizeRegion(x, y, width, height))
        return;

    appendColorRegion(x, y, width, height, toBe565(onColor), toBe565(offColor));
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
    // Use theme-appropriate menu colors.
    const TFTThemeDef &theme = kThemes[resolveThemeIndex()];
    const TFTThemeRoleColor &menuBody = theme.roles[static_cast<size_t>(TFTColorRole::ActionMenuBody)];
    const TFTThemeRoleColor &menuBorder = theme.roles[static_cast<size_t>(TFTColorRole::ActionMenuBorder)];

    // Fill role includes a 1px shadow guard so stale frame edges are overwritten uniformly.
    setAndRegisterTFTColorRole(TFTColorRole::ActionMenuBody, menuBody.onColor, menuBody.offColor, boxLeft - 1, boxTop - 1,
                               boxWidth + 2, boxHeight + 2);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop - 2, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft - 2, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft + boxWidth + 1, boxTop, 1, boxHeight);

    setAndRegisterTFTColorRole(TFTColorRole::ActionMenuBorder, menuBorder.onColor, menuBorder.offColor, boxLeft, boxTop, boxWidth,
                               1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop + boxHeight - 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft + boxWidth - 1, boxTop, 1, boxHeight);
#endif
}

// Frame signature & utilities

uint32_t getTFTColorFrameSignature()
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return 0;
#else
    uint32_t hash = kFnv1aOffsetBasis;
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

uint8_t getTFTColorRegionCount()
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return 0;
#else
    return colorRegionCount;
#endif
}

void clearTFTColorRegions()
{
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
    for (int i = static_cast<int>(colorRegionCount) - 1; i >= 0; i--) {
        const TFTColorRegion &r = colorRegions[i];
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            return isset ? r.onColorBe : r.offColorBe;
        }
    }
    return isset ? defaultOnColor : defaultOffColor;
}

uint16_t resolveTFTOffColorAt(int16_t x, int16_t y, uint16_t defaultOffColor)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    (void)x;
    (void)y;
    return defaultOffColor;
#else
    const uint16_t defaultOffBe = toBe565(defaultOffColor);
    const uint16_t sampledBe = resolveTFTColorPixel(x, y, false, defaultOffBe, defaultOffBe);
    return static_cast<uint16_t>((sampledBe >> 8) | (sampledBe << 8));
#endif
}

} // namespace graphics
