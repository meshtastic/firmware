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
void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *text);

// Text and emote rendering
void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount);

// Time and date utilities
void getTimeAgoStr(uint32_t agoSecs, char *timeStr, uint8_t maxLength);
std::string drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds);
int formatDateTime(char *buffer, size_t bufferSize, uint32_t rtc_sec, OLEDDisplay *display, bool showTime);

// Message filtering
bool shouldDrawMessage(const meshtastic_MeshPacket *packet);

} // namespace UIRenderer

} // namespace graphics
