#pragma once

#include "graphics/Screen.h"
#include "graphics/emotes.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <string>

#define HOURS_IN_MONTH 730

// Forward declarations for status types
namespace meshtastic
{
class PowerStatus;
class NodeStatus;
class GPSStatus;
} // namespace meshtastic

namespace graphics
{

/// Forward declarations
class Screen;

/**
 * @brief UI utility drawing functions
 *
 * Contains utility functions for drawing common UI elements, overlays,
 * battery indicators, and other shared graphical components.
 */
class UIRenderer
{
  public:
    // Common UI elements
    static void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::NodeStatus *nodeStatus,
                          int node_offset = 0, bool show_total = true, String additional_words = "");

    // GPS status functions
    static void drawGps(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
    static void drawGpsCoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
    static void drawGpsAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
    static void drawGpsPowerStatus(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);

    // Layout and utility functions
    static void drawScrollbar(OLEDDisplay *display, int visibleItems, int totalItems, int scrollIndex, int x, int startY);

    // Overlay and special screens
    static void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *text);

    // Navigation bar overlay
    static void drawNavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state);

    static void drawNodeInfo(OLEDDisplay *display, const OLEDDisplayUiState *state, int16_t x, int16_t y);

    static void drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    // Icon and screen drawing functions
    static void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    // Compass and location screen
    static void drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    static NodeNum currentFavoriteNodeNum;
    static std::vector<meshtastic_NodeInfoLite *> favoritedNodes;
    static void rebuildFavoritedNodes();

// OEM screens
#ifdef USERPREFS_OEM_TEXT
    static void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif

#ifdef USE_EINK
    /// Used on eink displays while in deep sleep
    static void drawDeepSleepFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    /// Used on eink displays when screen updates are paused
    static void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
#endif

    static std::string drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds);
    static int formatDateTime(char *buffer, size_t bufferSize, uint32_t rtc_sec, OLEDDisplay *display, bool showTime);

    // Message filtering
    static bool shouldDrawMessage(const meshtastic_MeshPacket *packet);
    // Check if the display can render a string (detect special chars; emoji)
    static bool haveGlyphs(const char *str);
}; // namespace UIRenderer

} // namespace graphics
