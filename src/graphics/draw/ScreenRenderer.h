#pragma once

#include "graphics/Screen.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

/// Forward declarations
class Screen;

/**
 * @brief Screen-specific drawing functions
 *
 * Contains drawing functions for specific screen types like GPS location,
 * memory usage, device info, and other specialized screens.
 */
namespace ScreenRenderer
{
// Screen frame functions
void drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawMemoryScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Text message screen
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Module and firmware frames
void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

} // namespace ScreenRenderer

} // namespace graphics
