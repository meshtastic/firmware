#pragma once

#include "graphics/Screen.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

/// Forward declarations
class Screen;

/**
 * @brief Radar/minimap view showing nearby nodes by bearing and distance.
 *
 * Renders a circular radar display with range rings. The user's node sits at
 * the centre; other nodes with valid GPS positions are plotted as small squares
 * at their true bearing and proportional distance.  A north indicator is drawn
 * at the top of the circle and a scale label shows the range of the outermost
 * ring in the info panel to the right.
 */
namespace RadarRenderer
{

void drawRadarScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

} // namespace RadarRenderer

} // namespace graphics
