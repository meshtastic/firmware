#pragma once

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

/// Forward declarations
class Screen;
class DebugInfo;

/**
 * @brief Debug and diagnostic drawing functions
 *
 * Contains all functions related to drawing debug information,
 * WiFi status, settings screens, and diagnostic data.
 */
namespace DebugRenderer
{
// Debug frame functions
void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Trampoline functions for framework callback compatibility
void drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// LoRa information display
void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// System screen display
void drawSystemScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
} // namespace DebugRenderer

} // namespace graphics
