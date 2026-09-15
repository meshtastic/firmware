#pragma once

#include "graphics/Screen.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

class Screen;

/**
 * @brief Radar overlay shown in place of the bearings/distance frame.
 *
 * Draws a node list on the left and a circular radar minimap on the right.
 * The user's node sits at the centre; remote nodes with valid positions are
 * plotted as small markers at their true bearing and proportional distance.
 *
 * When the BMX160 (RAK12034) is connected the radar is heading-up (the
 * direction the device faces is at the top).  A "N" label rotates to show
 * true north.  Without IMU the display is north-up.
 *
 * Heading mode and zoom level are toggled via the long-press radar menu.
 */
namespace RadarRenderer
{

// ---- Header + content renderer (called from drawDynamicListScreen_Location).
// Draws its own header ("Radar <scale>") so the title can carry the current
// outer-ring range; the caller still draws the footer.
void drawRadarOverlay(OLEDDisplay *display, int16_t x, int16_t y);

// ---- Runtime state (controlled by radarBearingsMenu) ------------------------

/** Returns true when forced north-up is active (overriding IMU). */
bool isNorthUp();

/** Toggle forced north-up / heading-up mode. */
void toggleNorthUp();

/**
 * Zoom level relative to the auto-calculated scale.
 *  0  = auto (default)
 * -1  = zoom in
 * -2  = zoom in (further)
 *  +1 = zoom out
 *  +2 = zoom out (further)
 * Clamped to [-2, +2].
 */
void zoomIn();
void zoomOut();

} // namespace RadarRenderer
} // namespace graphics
