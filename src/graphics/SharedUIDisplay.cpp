#include "graphics/SharedUIDisplay.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "power.h"
#include "meshtastic/config.pb.h"
#include "RTC.h"
#include <OLEDDisplay.h>
#include <graphics/images.h>

namespace graphics {

// === Shared External State ===
bool hasUnreadMessage = false;

// === Internal State ===
bool isBoltVisibleShared = true;
uint32_t lastBlinkShared = 0;
bool isMailIconVisible = true;
uint32_t lastMailBlink = 0;

// *********************************
// * Rounded Header when inverted *
// *********************************
void drawRoundedHighlight(OLEDDisplay *display, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r)
{
    display->fillRect(x + r, y, w - 2 * r, h);
    display->fillRect(x, y + r, r, h - 2 * r);
    display->fillRect(x + w - r, y + r, r, h - 2 * r);
    display->fillCircle(x + r + 1, y + r, r);
    display->fillCircle(x + w - r - 1, y + r, r);
    display->fillCircle(x + r + 1, y + h - r - 1, r);
    display->fillCircle(x + w - r - 1, y + h - r - 1, r);
}

// *************************
// * Common Header Drawing *
// *************************
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y)
{
    constexpr int HEADER_OFFSET_Y = 1;
    y += HEADER_OFFSET_Y;

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const int xOffset = 4;
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const bool isInverted = (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
    const bool isBold = config.display.heading_bold;

    const int screenW = display->getWidth();
    const int screenH = display->getHeight();

    if (isInverted) {
        drawRoundedHighlight(display, x, y, screenW, highlightHeight, 2);
        display->setColor(BLACK);
    }

    int chargePercent = powerStatus->getBatteryChargePercent();
    bool isCharging = powerStatus->getIsCharging() == meshtastic::OptionalBool::OptTrue;
    uint32_t now = millis();
    if (isCharging && now - lastBlinkShared > 500) {
        isBoltVisibleShared = !isBoltVisibleShared;
        lastBlinkShared = now;
    }

    bool useHorizontalBattery = (screenW > 128 && screenW > screenH);
    const int textY = y + (highlightHeight - FONT_HEIGHT_SMALL) / 2;

    // === Battery Icons ===
    if (useHorizontalBattery) {
        int batteryX = 2;
        int batteryY = HEADER_OFFSET_Y + 2;
        display->drawXbm(batteryX, batteryY, 29, 15, batteryBitmap_h);
        if (isCharging && isBoltVisibleShared) {
            display->drawXbm(batteryX + 9, batteryY + 1, 9, 13, lightning_bolt_h);
        } else {
            display->drawXbm(batteryX + 8, batteryY, 12, 15, batteryBitmap_sidegaps_h);
            int fillWidth = 24 * chargePercent / 100;
            display->fillRect(batteryX + 1, batteryY + 1, fillWidth, 13);
        }
    } else {
        int batteryX = 1;
        int batteryY = HEADER_OFFSET_Y + 1;
    #ifdef USE_EINK
        batteryY += 2;
    #endif
        display->drawXbm(batteryX, batteryY, 7, 11, batteryBitmap_v);
        if (isCharging && isBoltVisibleShared) {
            display->drawXbm(batteryX + 1, batteryY + 3, 5, 5, lightning_bolt_v);
        } else {
            display->drawXbm(batteryX - 1, batteryY + 4, 8, 3, batteryBitmap_sidegaps_v);
            int fillHeight = 8 * chargePercent / 100;
            int fillY = batteryY - fillHeight;
            display->fillRect(batteryX + 1, fillY + 10, 5, fillHeight);
        }
    }

    // === Battery % Text ===
    char chargeStr[4];
    snprintf(chargeStr, sizeof(chargeStr), "%d", chargePercent);
    int chargeNumWidth = display->getStringWidth(chargeStr);
    const int batteryOffset = useHorizontalBattery ? 28 : 6;
#ifdef USE_EINK
    const int percentX = x + xOffset + batteryOffset - 2;
#else
    const int percentX = x + xOffset + batteryOffset;
#endif
    display->drawString(percentX, textY, chargeStr);
    display->drawString(percentX + chargeNumWidth - 1, textY, "%");
    if (isBold) {
        display->drawString(percentX + 1, textY, chargeStr);
        display->drawString(percentX + chargeNumWidth, textY, "%");
    }

    // === Time and Mail Icon ===
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (rtc_sec > 0) {
        long hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
        int hour = hms / SEC_PER_HOUR;
        int minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;

        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", hour, minute);
        if (config.display.use_12h_clock) {
            bool isPM = hour >= 12;
            hour %= 12;
            if (hour == 0) hour = 12;
            snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", hour, minute, isPM ? "p" : "a");
        }

        int timeStrWidth = display->getStringWidth(timeStr);
        int timeX = screenW - xOffset - timeStrWidth + 4;

        if (hasUnreadMessage) {
            if (now - lastMailBlink > 500) {
                isMailIconVisible = !isMailIconVisible;
                lastMailBlink = now;
            }

            if (isMailIconVisible) {
                if (useHorizontalBattery) {
                    int iconW = 16, iconH = 12;
                    int iconX = timeX - iconW - 3;
                    int iconY = textY + (FONT_HEIGHT_SMALL - iconH) / 2 - 1;
                    display->drawRect(iconX, iconY, iconW, iconH);
                    display->drawLine(iconX + 1, iconY + 1, iconX + iconW / 2, iconY + iconH - 2);
                    display->drawLine(iconX + iconW - 2, iconY + 1, iconX + iconW / 2, iconY + iconH - 2);
                } else {
                    int iconX = timeX - mail_width;
                    int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;
                    display->drawXbm(iconX, iconY, mail_width, mail_height, mail);
                }
            }
        }

        display->drawString(timeX, textY, timeStr);
        if (isBold) display->drawString(timeX - 1, textY, timeStr);
    }

    display->setColor(WHITE);
}

} // namespace graphics
