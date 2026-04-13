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
    UtilizationFill,
    FavoriteNode,
    ActionMenuBorder,
    ActionMenuBody,
    ActionMenuTitle,
    FrameMono,
    BootSplash,
    BodyYellow,
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
void registerTFTActionMenuRegions(int16_t boxLeft, int16_t boxTop, int16_t boxWidth, int16_t boxHeight);
uint32_t getTFTColorFrameSignature();
uint8_t getTFTColorRegionCount();
void clearTFTColorRegions();
uint16_t resolveTFTColorPixel(int16_t x, int16_t y, bool isset, uint16_t defaultOnColor, uint16_t defaultOffColor);

// ── Theme engine ──────────────────────────────────────────────────────
// Theme IDs stored in uiconfig.screen_rgb_color.  Adding a new theme is
// just a new constant here + a kThemes[] entry in TFTColorRegions.cpp.
namespace ThemeID
{
constexpr uint32_t DefaultDark = 0;
constexpr uint32_t DefaultLight = 1;
constexpr uint32_t Christmas = 2;
constexpr uint32_t Pink = 3;
constexpr uint32_t Blue = 4;
constexpr uint32_t Creamsicle = 5;
} // namespace ThemeID

// Per-role color pair stored in native (little-endian) RGB565 format.
struct TFTThemeRoleColor {
    uint16_t onColor;
    uint16_t offColor;
};

// Complete theme definition.
struct TFTThemeDef {
    uint32_t id;      // ThemeID constant — must match kThemes[] index
    const char *name; // Human-readable label for the menu
    TFTThemeRoleColor roles[static_cast<size_t>(TFTColorRole::Count)];
    uint16_t headerBg;     // Header bar background
    uint16_t headerText;   // Header title text
    uint16_t headerStatus; // Header status icons / text
    uint16_t bodyBg;       // Default body / transparent-header background
    uint16_t bodyFg;       // Default body foreground
    bool fullFrameInvert;  // Apply full-frame FrameMono inversion (ST7789 light themes)
};

// Total number of built-in themes.
size_t getThemeCount();

// Access theme by array index (0 .. getThemeCount()-1).
const TFTThemeDef &getThemeByIndex(size_t index);

// Return the theme that matches uiconfig.screen_rgb_color (falls back to Dark).
const TFTThemeDef &getActiveTheme();

// Return the kThemes[] index for the currently active theme.
size_t getActiveThemeIndex();

// Convenience accessors – safe to call even when coloring is compiled out.
uint16_t getThemeHeaderBg();
uint16_t getThemeHeaderText();
uint16_t getThemeHeaderStatus();
uint16_t getThemeBodyBg();
uint16_t getThemeBodyFg();
bool isThemeFullFrameInvert();

// Reinitialise default roleColors from the active theme.  Call after a
// theme change so that any role registered without a prior setTFTColorRole()
// picks up theme-appropriate defaults.
void loadThemeDefaults();

} // namespace graphics
