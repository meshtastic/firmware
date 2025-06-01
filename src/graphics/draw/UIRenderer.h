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
namespace UIRenderer
{
// Common UI elements
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y);
void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const meshtastic::PowerStatus *powerStatus);
void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::NodeStatus *nodeStatus, int node_offset = 0,
               bool show_total = true, String additional_words = "");

// GPS status functions
void drawGps(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGpsCoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGpsAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGpsPowerStatus(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);

// Layout and utility functions
void drawScrollbar(OLEDDisplay *display, int visibleItems, int totalItems, int scrollIndex, int x, int startY);

// Overlay and special screens
void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *text);

// Function overlay for showing mute/buzzer modifiers etc.
void drawFunctionOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);

// Navigation bar overlay
void drawNavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state);

void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

void drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Icon and screen drawing functions
void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Compass and location screen
void drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// OEM screens
#ifdef USERPREFS_OEM_TEXT
void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
void drawDeepSleepFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/// Used on eink displays when screen updates are paused
void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
#endif

// Time and date utilities
void getTimeAgoStr(uint32_t agoSecs, char *timeStr, uint8_t maxLength);
std::string drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds);
int formatDateTime(char *buffer, size_t bufferSize, uint32_t rtc_sec, OLEDDisplay *display, bool showTime);

// Message filtering
bool shouldDrawMessage(const meshtastic_MeshPacket *packet);
// Check if the display can render a string (detect special chars; emoji)
bool haveGlyphs(const char *str);
} // namespace UIRenderer

} // namespace graphics
