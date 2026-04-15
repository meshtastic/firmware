#pragma once

#include "graphics/Screen.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

class Screen;

/**
 * @brief Radar/minimap screen showing nearby nodes by bearing and distance.
 *
 * Renders a circular radar with three labelled range rings.  The user's node
 * sits at the centre; other nodes with valid GPS positions are plotted as
 * 3×3 squares at their true bearing and proportional distance.
 *
 * When the BMX160 (RAK12034) is connected the display is heading-up: the
 * direction the device faces is always at the top.  A "N" label rotates to
 * show true north.  Without IMU the display is north-up.
 *
 * The heading mode and zoom level can be overridden at runtime via the
 * long-press menu (radarMenu in MenuHandler).
 */
namespace RadarRenderer
{

// ---- Frame callback ---------------------------------------------------------
void drawRadarScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// ---- Runtime state (controlled by radarMenu) --------------------------------

/** Returns true when forced north-up is active (overriding IMU). */
bool isNorthUp();

/** Toggle forced north-up / heading-up mode. */
void toggleNorthUp();

/**
 * Zoom level relative to the auto-calculated scale.
 *  0  = auto (default)
 * -1  = zoom in  (scale halved)
 * -2  = zoom in  (scale quartered)
 *  +1 = zoom out (scale doubled)
 *  +2 = zoom out (scale quadrupled)
 * Clamped to [-2, +2].
 */
void zoomIn();
void zoomOut();

} // namespace RadarRenderer
} // namespace graphics
