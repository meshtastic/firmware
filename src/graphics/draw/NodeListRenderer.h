#pragma once

#include "graphics/Screen.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

/// Forward declarations
class Screen;

/**
 * @brief Node list and entry rendering functions
 *
 * Contains all functions related to drawing node lists and individual node entries
 * including last heard, hop signal, distance, and compass views.
 */
namespace NodeListRenderer
{
// Entry renderer function types
typedef void (*EntryRenderer)(OLEDDisplay *, meshtastic_NodeInfoLite *, int16_t, int16_t, int);
typedef void (*NodeExtrasRenderer)(OLEDDisplay *, meshtastic_NodeInfoLite *, int16_t, int16_t, int, float, double, double);

// Node list mode enumeration
enum NodeListMode { MODE_LAST_HEARD = 0, MODE_HOP_SIGNAL = 1, MODE_DISTANCE = 2, MODE_COUNT = 3 };

// Main node list screen function
void drawNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                        EntryRenderer renderer, NodeExtrasRenderer extras = nullptr, float heading = 0, double lat = 0,
                        double lon = 0);

// Entry renderers
void drawEntryLastHeard(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth);
void drawEntryHopSignal(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth);
void drawNodeDistance(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth);
void drawEntryDynamic(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth);
void drawEntryCompass(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth);

// Extras renderers
void drawCompassArrow(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth, float myHeading,
                      double userLat, double userLon);

// Screen frame functions
void drawLastHeardScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawHopSignalScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDistanceScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDynamicNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawNodeListWithCompasses(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Utility functions
const char *getCurrentModeTitle(int screenWidth);
const char *getSafeNodeName(meshtastic_NodeInfoLite *node);
void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields);

// Bitmap drawing function
void drawScaledXBitmap16x16(int x, int y, int width, int height, const uint8_t *bitmapXBM, OLEDDisplay *display);

} // namespace NodeListRenderer

} // namespace graphics
