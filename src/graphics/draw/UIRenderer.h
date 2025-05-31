#pragma once

#include "graphics/Screen.h"
#include "graphics/emotes.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <string>

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
void drawGPS(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGPScoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGPSAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);
void drawGPSpowerstat(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gpsStatus);

// Layout and utility functions
void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields);
void drawColumnSeparator(OLEDDisplay *display, int16_t x, int16_t startY, int16_t endY);
void drawScrollbar(OLEDDisplay *display, int visibleItems, int totalItems, int scrollIndex, int x, int startY);

// Overlay and special screens
void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *text);

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
static void drawDeepSleepScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Draw deep sleep screen");

    // Display displayStr on the screen
    graphics::UIRenderer::drawIconScreen("Sleeping", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Draw screensaver overlay");

    EINK_ADD_FRAMEFLAG(display, COSMETIC); // Take the opportunity for a full-refresh

    // Config
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *pauseText = "Screen Paused";
    const char *idText = owner.short_name;
    const bool useId = haveGlyphs(idText); // This bool is used to hide the idText box if we can't render the short name
    constexpr uint16_t padding = 5;
    constexpr uint8_t dividerGap = 1;
    constexpr uint8_t imprecision = 5; // How far the box origins can drift from center. Combat burn-in.

    // Dimensions
    const uint16_t idTextWidth = display->getStringWidth(idText, strlen(idText), true); // "true": handle utf8 chars
    const uint16_t pauseTextWidth = display->getStringWidth(pauseText, strlen(pauseText));
    const uint16_t boxWidth = padding + (useId ? idTextWidth + padding + padding : 0) + pauseTextWidth + padding;
    const uint16_t boxHeight = padding + FONT_HEIGHT_SMALL + padding;

    // Position
    const int16_t boxLeft = (display->width() / 2) - (boxWidth / 2) + random(-imprecision, imprecision + 1);
    // const int16_t boxRight = boxLeft + boxWidth - 1;
    const int16_t boxTop = (display->height() / 2) - (boxHeight / 2 + random(-imprecision, imprecision + 1));
    const int16_t boxBottom = boxTop + boxHeight - 1;
    const int16_t idTextLeft = boxLeft + padding;
    const int16_t idTextTop = boxTop + padding;
    const int16_t pauseTextLeft = boxLeft + (useId ? padding + idTextWidth + padding : 0) + padding;
    const int16_t pauseTextTop = boxTop + padding;
    const int16_t dividerX = boxLeft + padding + idTextWidth + padding;
    const int16_t dividerTop = boxTop + 1 + dividerGap;
    const int16_t dividerBottom = boxBottom - 1 - dividerGap;

    // Draw: box
    display->setColor(EINK_WHITE);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Clear a slightly oversized area for the box
    display->setColor(EINK_BLACK);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

    // Draw: Text
    if (useId)
        display->drawString(idTextLeft, idTextTop, idText);
    display->drawString(pauseTextLeft, pauseTextTop, pauseText);
    display->drawString(pauseTextLeft + 1, pauseTextTop, pauseText); // Faux bold

    // Draw: divider
    if (useId)
        display->drawLine(dividerX, dividerTop, dividerX, dividerBottom);
}
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
