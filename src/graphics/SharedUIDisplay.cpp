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

void determineResolution(int16_t screenheight, int16_t screenwidth)
{
    if (screenwidth > 128) {
        isHighResolution = true;
    }

    // Special case for Heltec Wireless Tracker v1.1
    if (screenwidth == 160 && screenheight == 80) {
        isHighResolution = false;
    }
}

// === Shared External State ===
bool hasUnreadMessage = false;
bool isMuted = false;
bool isHighResolution = false;

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
    display->fillCircle(x + r + 1, y + r, r);             // top-left
    display->fillCircle(x + w - r - 1, y + r, r);         // top-right
    display->fillCircle(x + r + 1, y + h - r - 1, r);     // bottom-left
    display->fillCircle(x + w - r - 1, y + h - r - 1, r); // bottom-right
}

// *************************
// * Common Header Drawing *
// *************************
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr, bool battery_only)
{
    constexpr int HEADER_OFFSET_Y = 1;
    y += HEADER_OFFSET_Y;

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const int xOffset = 4;
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const bool isInverted = (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
    const bool isBold = config.display.heading_bold;

    const int screenW = display->getWidth();
    const int screenH = display->getHeight();

    if (!battery_only) {
        // === Inverted Header Background ===
        if (isInverted) {
            display->setColor(BLACK);
            display->fillRect(0, 0, screenW, highlightHeight + 2);
            display->setColor(WHITE);
            drawRoundedHighlight(display, x, y, screenW, highlightHeight, 2);
            display->setColor(BLACK);
        } else {
            display->setColor(BLACK);
            display->fillRect(0, 0, screenW, highlightHeight + 2);
            display->setColor(WHITE);
            if (isHighResolution) {
                display->drawLine(0, 20, screenW, 20);
            } else {
                display->drawLine(0, 14, screenW, 14);
            }
        }

        // === Screen Title ===
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(SCREEN_WIDTH / 2, y, titleStr);
        if (config.display.heading_bold) {
            display->drawString((SCREEN_WIDTH / 2) + 1, y, titleStr);
        }
    }
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Battery State ===
    int chargePercent = powerStatus->getBatteryChargePercent();
    bool isCharging = powerStatus->getIsCharging();
    bool usbPowered = powerStatus->getHasUSB();

    if (chargePercent >= 100) {
        isCharging = false;
    }
    if (chargePercent == 101) {
        usbPowered = true; // Forcing this flag on for the express purpose that some devices have no concept of having a USB cable
                           // plugged in
    }

    uint32_t now = millis();

#ifndef USE_EINK
    if (isCharging && now - lastBlinkShared > 500) {
        isBoltVisibleShared = !isBoltVisibleShared;
        lastBlinkShared = now;
    }
#endif

    bool useHorizontalBattery = (isHighResolution && screenW >= screenH);
    const int textY = y + (highlightHeight - FONT_HEIGHT_SMALL) / 2;

    int batteryX = 1;
    int batteryY = HEADER_OFFSET_Y + 1;

    // === Battery Icons ===
    if (usbPowered && !isCharging) { // This is a basic check to determine USB Powered is flagged but not charging
        batteryX += 1;
        batteryY += 2;
        if (isHighResolution) {
            display->drawXbm(batteryX, batteryY, 19, 12, imgUSB_HighResolution);
            batteryX += 20; // Icon + 1 pixel
        } else {
            display->drawXbm(batteryX, batteryY, 10, 8, imgUSB);
            batteryX += 11; // Icon + 1 pixel
        }
    } else {
        if (useHorizontalBattery) {
            batteryX += 1;
            batteryY += 2;
            display->drawXbm(batteryX, batteryY, 9, 13, batteryBitmap_h_bottom);
            display->drawXbm(batteryX + 9, batteryY, 9, 13, batteryBitmap_h_top);
            if (isCharging && isBoltVisibleShared)
                display->drawXbm(batteryX + 4, batteryY, 9, 13, lightning_bolt_h);
            else {
                display->drawLine(batteryX + 5, batteryY, batteryX + 10, batteryY);
                display->drawLine(batteryX + 5, batteryY + 12, batteryX + 10, batteryY + 12);
                int fillWidth = 14 * chargePercent / 100;
                display->fillRect(batteryX + 1, batteryY + 1, fillWidth, 11);
            }
            batteryX += 18; // Icon + 2 pixels
        } else {
#ifdef USE_EINK
            batteryY += 2;
#endif
            display->drawXbm(batteryX, batteryY, 7, 11, batteryBitmap_v);
            if (isCharging && isBoltVisibleShared)
                display->drawXbm(batteryX + 1, batteryY + 3, 5, 5, lightning_bolt_v);
            else {
                display->drawXbm(batteryX - 1, batteryY + 4, 8, 3, batteryBitmap_sidegaps_v);
                int fillHeight = 8 * chargePercent / 100;
                int fillY = batteryY - fillHeight;
                display->fillRect(batteryX + 1, fillY + 10, 5, fillHeight);
            }
            batteryX += 9; // Icon + 2 pixels
        }
    }

    if (chargePercent != 101) {
        // === Battery % Display ===
        char chargeStr[4];
        snprintf(chargeStr, sizeof(chargeStr), "%d", chargePercent);
        int chargeNumWidth = display->getStringWidth(chargeStr);
        display->drawString(batteryX, textY, chargeStr);
        display->drawString(batteryX + chargeNumWidth - 1, textY, "%");
        if (isBold) {
            display->drawString(batteryX + 1, textY, chargeStr);
            display->drawString(batteryX + chargeNumWidth, textY, "%");
        }
    }

    // === Time and Right-aligned Icons ===
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    char timeStr[10] = "--:--";                          // Fallback display
    int timeStrWidth = display->getStringWidth("12:34"); // Default alignment
    int timeX = screenW - xOffset - timeStrWidth + 4;

    if (rtc_sec > 0 && !battery_only) {
        // === Build Time String ===
        long hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
        int hour = hms / SEC_PER_HOUR;
        int minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", hour, minute);

        if (config.display.use_12h_clock) {
            bool isPM = hour >= 12;
            hour %= 12;
            if (hour == 0)
                hour = 12;
            snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", hour, minute, isPM ? "p" : "a");
        }

        timeStrWidth = display->getStringWidth(timeStr);
        timeX = screenW - xOffset - timeStrWidth + 3;

        // === Show Mail or Mute Icon to the Left of Time ===
        int iconRightEdge = timeX - 2;

        bool showMail = false;

#ifndef USE_EINK
        if (hasUnreadMessage) {
            if (now - lastMailBlink > 500) {
                isMailIconVisible = !isMailIconVisible;
                lastMailBlink = now;
            }
            showMail = isMailIconVisible;
        }
#else
        if (hasUnreadMessage) {
            showMail = true;
        }
#endif

        if (showMail) {
            if (useHorizontalBattery) {
                int iconW = 16, iconH = 12;
                int iconX = iconRightEdge - iconW;
                int iconY = textY + (FONT_HEIGHT_SMALL - iconH) / 2 - 1;
                if (isInverted) {
                    display->setColor(WHITE);
                    display->fillRect(iconX - 1, iconY - 1, iconW + 3, iconH + 2);
                    display->setColor(BLACK);
                } else {
                    display->setColor(BLACK);
                    display->fillRect(iconX - 1, iconY - 1, iconW + 3, iconH + 2);
                    display->setColor(WHITE);
                }
                display->drawRect(iconX, iconY, iconW + 1, iconH);
                display->drawLine(iconX, iconY, iconX + iconW / 2, iconY + iconH - 4);
                display->drawLine(iconX + iconW, iconY, iconX + iconW / 2, iconY + iconH - 4);
            } else {
                int iconX = iconRightEdge - (mail_width - 2);
                int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;
                if (isInverted) {
                    display->setColor(WHITE);
                    display->fillRect(iconX - 1, iconY - 1, mail_width + 2, mail_height + 2);
                    display->setColor(BLACK);
                } else {
                    display->setColor(BLACK);
                    display->fillRect(iconX - 1, iconY - 1, mail_width + 2, mail_height + 2);
                    display->setColor(WHITE);
                }
                display->drawXbm(iconX, iconY, mail_width, mail_height, mail);
            }
        } else if (isMuted) {
            if (isHighResolution) {
                int iconX = iconRightEdge - mute_symbol_big_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mute_symbol_big_height) / 2;

                if (isInverted) {
                    display->setColor(WHITE);
                    display->fillRect(iconX - 1, iconY - 1, mute_symbol_big_width + 2, mute_symbol_big_height + 2);
                    display->setColor(BLACK);
                } else {
                    display->setColor(BLACK);
                    display->fillRect(iconX - 1, iconY - 1, mute_symbol_big_width + 2, mute_symbol_big_height + 2);
                    display->setColor(WHITE);
                }
                display->drawXbm(iconX, iconY, mute_symbol_big_width, mute_symbol_big_height, mute_symbol_big);
            } else {
                int iconX = iconRightEdge - mute_symbol_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;

                if (isInverted) {
                    display->setColor(WHITE);
                    display->fillRect(iconX - 1, iconY - 1, mute_symbol_width + 2, mute_symbol_height + 2);
                    display->setColor(BLACK);
                } else {
                    display->setColor(BLACK);
                    display->fillRect(iconX - 1, iconY - 1, mute_symbol_width + 2, mute_symbol_height + 2);
                    display->setColor(WHITE);
                }
                display->drawXbm(iconX, iconY, mute_symbol_width, mute_symbol_height, mute_symbol);
            }
        }

        // === Draw Time ===
        display->drawString(timeX, textY, timeStr);
        if (isBold)
            display->drawString(timeX - 1, textY, timeStr);

    } else {
        // === No Time Available: Mail/Mute Icon Moves to Far Right ===
        int iconRightEdge = screenW - xOffset;

        bool showMail = false;

#ifndef USE_EINK
        if (hasUnreadMessage) {
            if (now - lastMailBlink > 500) {
                isMailIconVisible = !isMailIconVisible;
                lastMailBlink = now;
            }
            showMail = isMailIconVisible;
        }
#else
        if (hasUnreadMessage) {
            showMail = true;
        }
#endif

        if (showMail) {
            if (useHorizontalBattery) {
                int iconW = 16, iconH = 12;
                int iconX = iconRightEdge - iconW;
                int iconY = textY + (FONT_HEIGHT_SMALL - iconH) / 2 - 1;
                display->drawRect(iconX, iconY, iconW + 1, iconH);
                display->drawLine(iconX, iconY, iconX + iconW / 2, iconY + iconH - 4);
                display->drawLine(iconX + iconW, iconY, iconX + iconW / 2, iconY + iconH - 4);
            } else {
                int iconX = iconRightEdge - mail_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;
                display->drawXbm(iconX, iconY, mail_width, mail_height, mail);
            }
        } else if (isMuted) {
            if (isHighResolution) {
                int iconX = iconRightEdge - mute_symbol_big_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mute_symbol_big_height) / 2;
                display->drawXbm(iconX, iconY, mute_symbol_big_width, mute_symbol_big_height, mute_symbol_big);
            } else {
                int iconX = iconRightEdge - mute_symbol_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mail_height) / 2;
                display->drawXbm(iconX, iconY, mute_symbol_width, mute_symbol_height, mute_symbol);
            }
        }
    }

    display->setColor(WHITE); // Reset for other UI
}

const int *getTextPositions(OLEDDisplay *display)
{
    static int textPositions[7]; // Static array that persists beyond function scope

    if (isHighResolution) {
        textPositions[0] = textZeroLine;
        textPositions[1] = textFirstLine_medium;
        textPositions[2] = textSecondLine_medium;
        textPositions[3] = textThirdLine_medium;
        textPositions[4] = textFourthLine_medium;
        textPositions[5] = textFifthLine_medium;
        textPositions[6] = textSixthLine_medium;
    } else {
        textPositions[0] = textZeroLine;
        textPositions[1] = textFirstLine;
        textPositions[2] = textSecondLine;
        textPositions[3] = textThirdLine;
        textPositions[4] = textFourthLine;
        textPositions[5] = textFifthLine;
        textPositions[6] = textSixthLine;
    }
    return textPositions;
}

bool isAllowedPunctuation(char c)
{
    const std::string allowed = ".,!?;:-_()[]{}'\"@#$/\\&+=%~^ ";
    return allowed.find(c) != std::string::npos;
}

std::string sanitizeString(const std::string &input)
{
    std::string output;
    bool inReplacement = false;

    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || isAllowedPunctuation(c)) {
            output += c;
            inReplacement = false;
        } else {
            if (!inReplacement) {
                output += 0xbf; // ISO-8859-1 for inverted question mark
                inReplacement = true;
            }
        }
    }

    return output;
}

} // namespace graphics
