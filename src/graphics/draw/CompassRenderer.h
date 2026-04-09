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
 * @brief Compass and navigation drawing functions
 *
 * Contains all functions related to drawing compass elements, headings,
 * navigation arrows, and location-based UI components.
 */
namespace CompassRenderer
{
// Compass drawing functions
void drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading, int16_t radius);
void drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, uint16_t compassDiam, float headingRadian);
void drawArrowToNode(OLEDDisplay *display, int16_t x, int16_t y, int16_t size, float bearing);

// Navigation and location functions
float estimatedHeading(double lat, double lon);
uint16_t getCompassDiam(uint32_t displayWidth, uint32_t displayHeight);

} // namespace CompassRenderer

} // namespace graphics
