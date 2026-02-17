#pragma once

#include <Arduino.h>

namespace graphics
{

// Sets clipping rectangle for subsequent overlays (inclusive bounds).
void setWeatherColorOverlayClip(int16_t left, int16_t top, int16_t right, int16_t bottom);

// Clears all queued overlays for the current frame.
void clearWeatherColorOverlays();

// Queues one monochrome XBM icon to be drawn in RGB565 color on TFT after the UI frame flush.
void queueWeatherColorOverlay(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm, uint16_t color565);

// Flushes all queued overlays to TFT (no-op on non-supported targets).
void flushWeatherColorOverlays();

} // namespace graphics
