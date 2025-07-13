#pragma once

#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"
#include "graphics/Screen.h"
#define MAX_LINES 5

namespace graphics
{

class NotificationRenderer
{
  public:
    static InputEvent inEvent;
    static char inKeypress;
    static int8_t curSelected;
    static char alertBannerMessage[256];
    static uint32_t alertBannerUntil; // 0 is a special case meaning forever
    static const char **optionsArrayPtr;
    static const int *optionsEnumPtr;
    static uint8_t alertBannerOptions; // last x lines are seelctable options
    static std::function<void(int)> alertBannerCallback;
    static uint32_t numDigits;
    static uint32_t currentNumber;

    static bool pauseBanner;

    static void resetBanner();
    static void drawBannercallback(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawNumberPicker(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawNodePicker(OLEDDisplay *display, OLEDDisplayUiState *state);
    static void drawNotificationBox(OLEDDisplay *display, OLEDDisplayUiState *state, const char *lines[MAX_LINES + 1],
                                    uint16_t totalLines, uint8_t firstOptionToShow, uint16_t maxWidth = 0);

    static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    static bool isOverlayBannerShowing();

    static graphics::notificationTypeEnum current_notification_type;
};

} // namespace graphics
