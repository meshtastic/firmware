#pragma once

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

/// Forward declarations
class Screen;

namespace ClockRenderer
{

// Clock frame functions
void drawAnalogClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDigitalClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Segmented display functions
void drawSegmentedDisplayCharacter(OLEDDisplay *display, int x, int y, uint8_t number, float scale = 1);
void drawSegmentedDisplayColon(OLEDDisplay *display, int x, int y, float scale = 1);
void drawHorizontalSegment(OLEDDisplay *display, int x, int y, int width, int height);
void drawVerticalSegment(OLEDDisplay *display, int x, int y, int width, int height);

// UI elements for clock displays
// void drawWatchFaceToggleButton(OLEDDisplay *display, int16_t x, int16_t y, bool digitalMode = true, float scale = 1);
void drawBluetoothConnectedIcon(OLEDDisplay *display, int16_t x, int16_t y);

} // namespace ClockRenderer

} // namespace graphics
