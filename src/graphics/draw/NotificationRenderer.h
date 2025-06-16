#pragma once

#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"

namespace graphics
{

class NotificationRenderer
{
  public:
    static char inEvent;
    static int8_t curSelected;
    static char alertBannerMessage[256];
    static uint32_t alertBannerUntil;  // 0 is a special case meaning forever
    static uint8_t alertBannerOptions; // last x lines are seelctable options
    static std::function<void(int)> alertBannerCallback;

    static bool pauseBanner;

    static void drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static bool isOverlayBannerShowing();
};

} // namespace graphics
