#include "graphics/SharedUIDisplay.h"
#include "RTC.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "meshtastic/config.pb.h"
#include "power.h"
#include <OLEDDisplay.h>
#include <graphics/images.h>

namespace graphics
{

// === Shared External State ===
bool hasUnreadMessage = false;
bool isMuted = false;

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
    // Draw the center and side rectangles
    display->fillRect(x + r, y, w - 2 * r, h);         // center bar
    display->fillRect(x, y + r, r, h - 2 * r);         // left edge
    display->fillRect(x + w - r, y + r, r, h - 2 * r); // right edge

    // Draw the rounded corners using filled circles
    display->fillCircle(x + r + 1, y + r, r);               // top-left
    display->fillCircle(x + w - r - 1, y + r, r);           // top-right
    display->fillCircle(x + r + 1, y + h - r - 1, r);       // bottom-left
    display->fillCircle(x + w - r - 1, y + h - r - 1, r);   // bottom-right
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

    // === Draw background highlight if inverted ===
    if (isInverted) {
        drawRoundedHighlight(display, x, y, screenW, highlightHeight, 2);
        display->setColor(BLACK);
    }

    // === Get battery charge percentage and charging status ===
    int chargePercent = powerStatus->getBatteryChargePercent();
    bool isCharging = powerStatus->getIsCharging() == meshtastic::OptionalBool::OptTrue;

    // === Animate lightning bolt blinking if charging ===
    uint32_t now = millis();
    if (isCharging && now - lastBlinkShared > 500) {
        isBoltVisibleShared = !isBoltVisibleShared;
        lastBlinkShared = now;
    }

    bool useHorizontalBattery = (screenW > 128 && screenW > screenH);
    const int textY = y + (highlightHeight - FONT_HEIGHT_SMALL) / 2;

    // === Draw battery icon ===
    if (useHorizontalBattery) {
        // Wide screen battery layout
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
        // Tall screen battery layout
        int batteryX = 1;
        int batteryY = HEADER_OFFSET_Y + 1;
#ifdef USE_EINK
        batteryY += 2; // Extra spacing on E-Ink
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

    // === Handle time display and alignment ===
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    char timeStr[10] = "";
    int alignX;

    if (rtc_sec > 0) {
        // Format time string (12h or 24h)
        long hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
        int hour = hms / SEC_PER_HOUR;
        int minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;

        snprintf(timeStr, sizeof(timeStr), "%d:%02d", hour, minute);
        if (config.display.use_12h_clock) {
            bool isPM = hour >= 12;
            hour %= 12;
            if (hour == 0) hour = 12;
            snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", hour, minute, isPM ? "p" : "a");
        }

        int timeStrWidth = display->getStringWidth(timeStr);
        alignX = screenW - xOffset - timeStrWidth + 4;
    } else {
        // If time is not valid, reserve space for alignment anyway
        int fallbackWidth = display->getStringWidth("12:34");
        alignX = screenW - xOffset - fallbackWidth + 4;
    }

    // === Determine if mail icon should blink ===
    bool showMail = false;
    if (hasUnreadMessage) {
        if (now - lastMailBlink > 500) {
            isMailIconVisible = !isMailIconVisible;
            lastMailBlink = now;
        }
        showMail = isMailIconVisible;
    }

    // === Draw Mail or Mute icon in the top-right corner ===
    if (showMail) {
        if (useHorizontalBattery) {
            int iconW = 16, iconH = 12;
            int iconX = screenW - xOffset - iconW;
            int iconY = textY + (FONT_HEIGHT_SMALL - iconH) / 2 - 1;
            display->drawRect(iconX, iconY, iconW + 1, iconH);
            display->drawLine(iconX, iconY, iconX + iconW / 2, iconY + iconH - 4);
            display->drawLine(iconX + iconW, iconY, iconX + iconW / 2, iconY + iconH - 4);
        } else {
            int iconX = screenW - xOffset - mail_width;
            int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;
            display->drawXbm(iconX, iconY, mail_width, mail_height, mail);
        }
    } else if (isMuted) {
        const char* muteStr = "M";
        int mWidth = display->getStringWidth(muteStr);
        int mX = screenW - xOffset - mWidth;
        display->drawString(mX, textY, muteStr);
        if (isBold)
            display->drawString(mX + 1, textY, muteStr);
    } else if (rtc_sec > 0) {
        // Only draw the time if nothing else is shown
        display->drawString(alignX, textY, timeStr);
        if (isBold)
            display->drawString(alignX - 1, textY, timeStr);
    }

    // === Reset color back to white for following content ===
    display->setColor(WHITE);
}

} // namespace graphics
