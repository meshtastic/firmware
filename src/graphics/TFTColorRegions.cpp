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

static inline bool isBodyColorRole(TFTColorRole role)
{
    switch (role) {
    case TFTColorRole::HeaderBackground:
    case TFTColorRole::HeaderTitle:
    case TFTColorRole::HeaderStatus:
    case TFTColorRole::BootSplash:
    case TFTColorRole::NavigationBar:
    case TFTColorRole::NavigationArrow:
        return false;
    default:
        return true;
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

// ── Compile-time header color overrides (backward-compatible) ─────────
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

// ── Theme definitions ─────────────────────────────────────────────────
// Stored in kThemes[] and looked up by matching uiconfig.screen_rgb_color
// against each entry's .id field.  Theme IDs live in ThemeID:: (header).

static const TFTThemeDef kThemes[] = {

    // ── Default Dark (ThemeID::DefaultDark = 0) ──────────────────────
    {
        ThemeID::DefaultDark, // id
        "Default Dark",       // name
        // roles[TFTColorRole::Count]
        {
            {kHeaderBackground, TFTPalette::Black},    // HeaderBackground
            {kHeaderBackground, kTitleColor},          // HeaderTitle
            {kHeaderBackground, kStatusColor},         // HeaderStatus
            {TFTPalette::Good, TFTPalette::Black},     // SignalBars
            {TFTPalette::Good, TFTPalette::Black},     // BatteryFill
            {TFTPalette::Blue, TFTPalette::Black},     // ConnectionIcon
            {TFTPalette::Good, TFTPalette::Black},     // UtilizationFill
            {TFTPalette::Yellow, TFTPalette::Black},   // FavoriteNode
            {TFTPalette::DarkGray, TFTPalette::Black}, // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Black},    // ActionMenuBody
            {TFTPalette::DarkGray, TFTPalette::White}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},    // FrameMono
            {TFTPalette::White, TFTPalette::Black},    // BootSplash
            {TFTPalette::Yellow, TFTPalette::Black},   // BodyYellow
            {kStatusColor, kHeaderBackground},         // NavigationBar  (icon fg, bar bg)
            {kTitleColor, TFTPalette::Black},          // NavigationArrow (arrow fg, body bg)
        },
        kHeaderBackground, // headerBg
        kTitleColor,       // headerText
        kStatusColor,      // headerStatus
        TFTPalette::Black, // bodyBg
        TFTPalette::White, // bodyFg
        false,             // fullFrameInvert
    },

    // ── Default Light (ThemeID::DefaultLight = 1) ────────────────────
    {
        ThemeID::DefaultLight, // id
        "Default Light",       // name
        {
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderTitle
            {TFTPalette::LightGray, TFTPalette::Black}, // HeaderStatus
            {TFTPalette::Good, TFTPalette::White},      // SignalBars
            {TFTPalette::Good, TFTPalette::White},      // BatteryFill
            {TFTPalette::Blue, TFTPalette::White},      // ConnectionIcon
            {TFTPalette::Good, TFTPalette::White},      // UtilizationFill
            {TFTPalette::Black, TFTPalette::Yellow},    // FavoriteNode
            {TFTPalette::DarkGray, TFTPalette::White},  // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::White},     // ActionMenuBody
            {TFTPalette::DarkGray, TFTPalette::Black},  // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},     // FrameMono
            {TFTPalette::White, TFTPalette::Black},     // BootSplash
            {TFTPalette::Black, TFTPalette::Yellow},    // BodyYellow
            {TFTPalette::Black, TFTPalette::LightGray}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::Black, TFTPalette::White},     // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::LightGray, // headerBg
        TFTPalette::Black,     // headerText
        TFTPalette::Black,     // headerStatus
        TFTPalette::White,     // bodyBg
        TFTPalette::Black,     // bodyFg
        true,                  // fullFrameInvert
    },

    // ── Christmas (ThemeID::Christmas = 2) ───────────────────────────
    {
        ThemeID::Christmas, // id
        "Christmas",        // name
        {
            {TFTPalette::ChristmasRed, TFTPalette::Black},  // HeaderBackground
            {TFTPalette::ChristmasRed, TFTPalette::Gold},   // HeaderTitle
            {TFTPalette::ChristmasRed, TFTPalette::Gold},   // HeaderStatus
            {TFTPalette::ChristmasGreen, TFTPalette::Pine}, // SignalBars
            {TFTPalette::ChristmasGreen, TFTPalette::Pine}, // BatteryFill
            {TFTPalette::Gold, TFTPalette::Pine},           // ConnectionIcon
            {TFTPalette::ChristmasGreen, TFTPalette::Pine}, // UtilizationFill
            {TFTPalette::Gold, TFTPalette::Pine},           // FavoriteNode
            {TFTPalette::ChristmasRed, TFTPalette::Pine},   // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Pine},          // ActionMenuBody
            {TFTPalette::ChristmasRed, TFTPalette::White},  // ActionMenuTitle
            {TFTPalette::Pine, TFTPalette::White},          // FrameMono
            {TFTPalette::White, TFTPalette::ChristmasRed},  // BootSplash
            {TFTPalette::Gold, TFTPalette::Pine},           // BodyYellow
            {TFTPalette::Gold, TFTPalette::ChristmasRed},   // NavigationBar  (icon fg, bar bg)
            {TFTPalette::Gold, TFTPalette::Pine},           // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::ChristmasRed, // headerBg
        TFTPalette::Gold,         // headerText
        TFTPalette::Gold,         // headerStatus
        TFTPalette::Pine,         // bodyBg
        TFTPalette::White,        // bodyFg
        true,                     // fullFrameInvert
    },

    // ── Pink (ThemeID::Pink = 3) — light variant ─────────────────────
    {
        ThemeID::Pink, // id
        "Pink",        // name
        {
            {TFTPalette::HotPink, TFTPalette::Black},     // HeaderBackground
            {TFTPalette::HotPink, TFTPalette::White},     // HeaderTitle
            {TFTPalette::HotPink, TFTPalette::White},     // HeaderStatus
            {TFTPalette::DeepPink, TFTPalette::PalePink}, // SignalBars
            {TFTPalette::DeepPink, TFTPalette::PalePink}, // BatteryFill
            {TFTPalette::HotPink, TFTPalette::PalePink},  // ConnectionIcon
            {TFTPalette::DeepPink, TFTPalette::PalePink}, // UtilizationFill
            {TFTPalette::Black, TFTPalette::HotPink},     // FavoriteNode
            {TFTPalette::HotPink, TFTPalette::PalePink},  // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::PalePink},    // ActionMenuBody
            {TFTPalette::HotPink, TFTPalette::White},     // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::HotPink},     // BootSplash
            {TFTPalette::Black, TFTPalette::HotPink},     // BodyYellow
            {TFTPalette::White, TFTPalette::HotPink},     // NavigationBar  (icon fg, bar bg)
            {TFTPalette::HotPink, TFTPalette::PalePink},  // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::HotPink,  // headerBg
        TFTPalette::White,    // headerText
        TFTPalette::White,    // headerStatus
        TFTPalette::PalePink, // bodyBg
        TFTPalette::Black,    // bodyFg
        true,                 // fullFrameInvert
    },

    // ── Blue (ThemeID::Blue = 4) — dark variant ──────────────────────
    {
        ThemeID::Blue, // id
        "Blue",        // name
        {
            {TFTPalette::DeepBlue, TFTPalette::Black},   // HeaderBackground
            {TFTPalette::DeepBlue, TFTPalette::White},   // HeaderTitle
            {TFTPalette::DeepBlue, TFTPalette::SkyBlue}, // HeaderStatus
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // SignalBars
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // BatteryFill
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // ConnectionIcon
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // UtilizationFill
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // FavoriteNode
            {TFTPalette::DeepBlue, TFTPalette::Navy},    // ActionMenuBorder
            {TFTPalette::White, TFTPalette::Navy},       // ActionMenuBody
            {TFTPalette::DeepBlue, TFTPalette::White},   // ActionMenuTitle
            {TFTPalette::Navy, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::DeepBlue},   // BootSplash
            {TFTPalette::SkyBlue, TFTPalette::Navy},     // BodyYellow
            {TFTPalette::SkyBlue, TFTPalette::DeepBlue}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::SkyBlue, TFTPalette::Black},    // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::DeepBlue, // headerBg
        TFTPalette::White,    // headerText
        TFTPalette::SkyBlue,  // headerStatus
        TFTPalette::Navy,     // bodyBg
        TFTPalette::White,    // bodyFg
        false,                // fullFrameInvert
    },

    // ── Creamsicle (ThemeID::Creamsicle = 5) — light variant ─────────
    {
        ThemeID::Creamsicle, // id
        "Creamsicle",        // name
        {
            {TFTPalette::CreamOrange, TFTPalette::Black}, // HeaderBackground
            {TFTPalette::CreamOrange, TFTPalette::White}, // HeaderTitle
            {TFTPalette::CreamOrange, TFTPalette::White}, // HeaderStatus
            {TFTPalette::DeepOrange, TFTPalette::Cream},  // SignalBars
            {TFTPalette::DeepOrange, TFTPalette::Cream},  // BatteryFill
            {TFTPalette::CreamOrange, TFTPalette::Cream}, // ConnectionIcon
            {TFTPalette::DeepOrange, TFTPalette::Cream},  // UtilizationFill
            {TFTPalette::Black, TFTPalette::CreamOrange}, // FavoriteNode
            {TFTPalette::CreamOrange, TFTPalette::Cream}, // ActionMenuBorder
            {TFTPalette::Black, TFTPalette::Cream},       // ActionMenuBody
            {TFTPalette::CreamOrange, TFTPalette::White}, // ActionMenuTitle
            {TFTPalette::Black, TFTPalette::White},       // FrameMono
            {TFTPalette::White, TFTPalette::CreamOrange}, // BootSplash
            {TFTPalette::Black, TFTPalette::CreamOrange}, // BodyYellow
            {TFTPalette::White, TFTPalette::CreamOrange}, // NavigationBar  (icon fg, bar bg)
            {TFTPalette::CreamOrange, TFTPalette::White}, // NavigationArrow (arrow fg, body bg)
        },
        TFTPalette::CreamOrange, // headerBg
        TFTPalette::White,       // headerText
        TFTPalette::White,       // headerStatus
        TFTPalette::Cream,       // bodyBg
        TFTPalette::Black,       // bodyFg
        true,                    // fullFrameInvert
    },
};

static constexpr size_t kInternalThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

// Resolve the kThemes[] index for the current screen_rgb_color value.
// Falls back to 0 (DefaultDark) if no match is found.
static inline size_t resolveThemeIndex()
{
    const uint32_t id = uiconfig.screen_rgb_color;
    for (size_t i = 0; i < kInternalThemeCount; i++) {
        if (kThemes[i].id == id)
            return i;
    }
    return 0; // Default Dark fallback
}

// Current working role colors (big-endian).  Initialised to Dark defaults;
// call loadThemeDefaults() after boot / theme change to refresh.
static TFTRoleColorsBe roleColors[static_cast<size_t>(TFTColorRole::Count)] = {
    {toBe565(kHeaderBackground), toBe565(TFTPalette::Black)},    // HeaderBackground
    {toBe565(kHeaderBackground), toBe565(kTitleColor)},          // HeaderTitle
    {toBe565(kHeaderBackground), toBe565(kStatusColor)},         // HeaderStatus
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // SignalBars
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // BatteryFill
    {toBe565(TFTPalette::Blue), toBe565(TFTPalette::Black)},     // ConnectionIcon
    {toBe565(TFTPalette::Good), toBe565(TFTPalette::Black)},     // UtilizationFill
    {toBe565(TFTPalette::Yellow), toBe565(TFTPalette::Black)},   // FavoriteNode
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::Black)}, // ActionMenuBorder
    {toBe565(TFTPalette::White), toBe565(TFTPalette::Black)},    // ActionMenuBody
    {toBe565(TFTPalette::DarkGray), toBe565(TFTPalette::White)}, // ActionMenuTitle
    {toBe565(TFTPalette::Black), toBe565(TFTPalette::White)},    // FrameMono
    {toBe565(TFTPalette::White), toBe565(TFTPalette::Black)},    // BootSplash
    {toBe565(TFTPalette::Yellow), toBe565(TFTPalette::Black)},   // BodyYellow
    {toBe565(kStatusColor), toBe565(kHeaderBackground)},         // NavigationBar
    {toBe565(kTitleColor), toBe565(TFTPalette::Black)}           // NavigationArrow
};

} // namespace

// ── Theme accessors ───────────────────────────────────────────────────

size_t getThemeCount()
{
    return kInternalThemeCount;
}

const TFTThemeDef &getThemeByIndex(size_t index)
{
    return kThemes[index < kInternalThemeCount ? index : 0];
}

const TFTThemeDef &getActiveTheme()
{
    return kThemes[resolveThemeIndex()];
}

size_t getActiveThemeIndex()
{
    return resolveThemeIndex();
}

uint16_t getThemeHeaderBg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
#ifdef TFT_HEADER_BG_COLOR_OVERRIDE
    return TFT_HEADER_BG_COLOR_OVERRIDE;
#else
    return kThemes[resolveThemeIndex()].headerBg;
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
    return kThemes[resolveThemeIndex()].headerText;
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
    return kThemes[resolveThemeIndex()].headerStatus;
#endif
#else
    return TFTPalette::White;
#endif
}

uint16_t getThemeBodyBg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    return kThemes[resolveThemeIndex()].bodyBg;
#else
    return TFTPalette::Black;
#endif
}

uint16_t getThemeBodyFg()
{
#if GRAPHICS_TFT_COLORING_ENABLED
    return kThemes[resolveThemeIndex()].bodyFg;
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

// ── Role color assignment with theme-aware transforms ─────────────────

void setTFTColorRole(TFTColorRole role, uint16_t onColor, uint16_t offColor)
{
#if !GRAPHICS_TFT_COLORING_ENABLED
    return;
#endif

    const uint32_t themeId = uiconfig.screen_rgb_color;

    // Highlight roles (FavoriteNode / BodyYellow) get per-theme accent treatment.
    if (role == TFTColorRole::FavoriteNode || role == TFTColorRole::BodyYellow) {
        switch (themeId) {
        case ThemeID::DefaultLight:
            // High-contrast: black glyphs on yellow background.
            onColor = TFTPalette::Black;
            offColor = TFTPalette::Yellow;
            break;
        case ThemeID::Christmas:
            // Gold accent on pine background.
            if (onColor == TFTPalette::Yellow)
                onColor = TFTPalette::Gold;
            if (offColor == TFTPalette::Black)
                offColor = TFTPalette::Pine;
            break;
        case ThemeID::Pink:
            // Pink light theme: high-contrast dark on pink.
            onColor = TFTPalette::Black;
            offColor = TFTPalette::HotPink;
            break;
        case ThemeID::Blue:
            // Blue accent on navy background.
            if (onColor == TFTPalette::Yellow)
                onColor = TFTPalette::SkyBlue;
            if (offColor == TFTPalette::Black)
                offColor = TFTPalette::Navy;
            break;
        case ThemeID::Creamsicle:
            // Orange-on-cream: high-contrast dark on orange.
            onColor = TFTPalette::Black;
            offColor = TFTPalette::CreamOrange;
            break;
        default:
            break;
        }
    } else if (isBodyColorRole(role)) {
        // Body / indicator roles: adjust to fit the active theme's palette.
        switch (themeId) {
        case ThemeID::DefaultLight:
            // Invert body colours for readability on white frames.
            if (offColor == TFTPalette::Black && role != TFTColorRole::ActionMenuTitle) {
                offColor = TFTPalette::White;
            }
            if (onColor == TFTPalette::White) {
                onColor = TFTPalette::Black;
            }
            break;
        case ThemeID::Christmas:
            // Swap yellow accents to gold; black backgrounds to pine.
            if (onColor == TFTPalette::Yellow)
                onColor = TFTPalette::Gold;
            if (offColor == TFTPalette::Black)
                offColor = TFTPalette::Pine;
            break;
        case ThemeID::Pink:
            // Invert body colours for readability on pale pink frames.
            if (offColor == TFTPalette::Black) {
                offColor = TFTPalette::PalePink;
            }
            if (onColor == TFTPalette::White) {
                onColor = TFTPalette::Black;
            }
            if (onColor == TFTPalette::Yellow) {
                onColor = TFTPalette::DeepPink;
            }
            break;
        case ThemeID::Blue:
            // Swap yellow accents to sky blue; black backgrounds to navy.
            if (onColor == TFTPalette::Yellow)
                onColor = TFTPalette::SkyBlue;
            if (offColor == TFTPalette::Black)
                offColor = TFTPalette::Navy;
            break;
        case ThemeID::Creamsicle:
            // Invert body colours for readability on cream frames.
            if (offColor == TFTPalette::Black) {
                offColor = TFTPalette::Cream;
            }
            if (onColor == TFTPalette::White) {
                onColor = TFTPalette::Black;
            }
            if (onColor == TFTPalette::Yellow) {
                onColor = TFTPalette::DeepOrange;
            }
            break;
        default:
            break;
        }
    }

    const uint8_t index = static_cast<uint8_t>(role);
    if (index >= static_cast<uint8_t>(TFTColorRole::Count)) {
        return;
    }

    roleColors[index].onColorBe = toBe565(onColor);
    roleColors[index].offColorBe = toBe565(offColor);
}

// ── Region registration ───────────────────────────────────────────────

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
    // Use theme-appropriate menu colors.
    const TFTThemeDef &theme = kThemes[resolveThemeIndex()];
    const TFTThemeRoleColor &menuBody = theme.roles[static_cast<size_t>(TFTColorRole::ActionMenuBody)];
    const TFTThemeRoleColor &menuBorder = theme.roles[static_cast<size_t>(TFTColorRole::ActionMenuBorder)];

    // Fill role includes a 1px shadow guard so stale frame edges are overwritten uniformly.
    setTFTColorRole(TFTColorRole::ActionMenuBody, menuBody.onColor, menuBody.offColor);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop - 2, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft - 2, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBody, boxLeft + boxWidth + 1, boxTop, 1, boxHeight);

    setTFTColorRole(TFTColorRole::ActionMenuBorder, menuBorder.onColor, menuBorder.offColor);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop + boxHeight - 1, boxWidth, 1);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft, boxTop, 1, boxHeight);
    registerTFTColorRegion(TFTColorRole::ActionMenuBorder, boxLeft + boxWidth - 1, boxTop, 1, boxHeight);
#endif
}

// ── Frame signature & utilities ───────────────────────────────────────

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

} // namespace graphics
