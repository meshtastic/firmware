#pragma once

#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"

namespace graphics
{

namespace NotificationRenderer
{

class NotificationRenderer
{
  public:
    static void drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawWelcomeScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
};

} // namespace NotificationRenderer

} // namespace graphics
