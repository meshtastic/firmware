#include "configuration.h"
#if HAS_SCREEN
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "draw/NodeListRenderer.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/draw/UIRenderer.h"
#include "main.h"
#include "meshtastic/config.pb.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include <OLEDDisplay.h>
#include <cctype>
#include <graphics/images.h>

namespace graphics
{

ScreenResolution determineScreenResolution(int16_t screenheight, int16_t screenwidth)
{

#ifdef FORCE_LOW_RES
    return ScreenResolution::Low;
#else
    // Unit C6L and other ultra low res screens
    if (screenwidth <= 64 || screenheight <= 48) {
        return ScreenResolution::UltraLow;
    }

    // Standard OLED screens
    if (screenwidth > 128 && screenheight <= 64) {
        return ScreenResolution::Low;
    }

    // High Resolutions screens like T114, TDeck, TLora Pager, etc
    if (screenwidth > 128) {
        return ScreenResolution::High;
    }

    // Default to low resolution
    return ScreenResolution::Low;
#endif
}

void decomposeTime(uint32_t rtc_sec, int &hour, int &minute, int &second)
{
    hour = 0;
    minute = 0;
    second = 0;
    if (rtc_sec == 0)
        return;
    uint32_t hms = (rtc_sec % SEC_PER_DAY + SEC_PER_DAY) % SEC_PER_DAY;
    hour = hms / SEC_PER_HOUR;
    minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
    second = hms % SEC_PER_MIN;
}

// === Shared External State ===
bool hasUnreadMessage = false;
ScreenResolution currentResolution = ScreenResolution::Low;

// === Internal State ===
bool isBoltVisibleShared = true;
uint32_t lastBlinkShared = 0;
bool isMailIconVisible = true;
uint32_t lastMailBlink = 0;

static inline bool useClockHeaderAccentTheme(uint32_t themeId)
{
    return themeId == ThemeID::Pink || themeId == ThemeID::Creamsicle || themeId == ThemeID::MeshtasticGreen ||
           themeId == ThemeID::ClassicRed || themeId == ThemeID::MonochromeWhite;
}

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
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr, bool force_no_invert, bool show_date,
                      bool transparent_background, bool use_title_color_override, uint16_t title_color_override)
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
    const int headerHeight = highlightHeight + 2;
    const uint16_t headerColorForRoles = getThemeHeaderBg();
    // Color TFT headers use a fixed dark background + white glyphs.
    // Keep legacy inverted bitmap behavior only for monochrome displays.
    const bool useInvertedHeaderStyle = (isInverted && !force_no_invert && !isTFTColoringEnabled() && !transparent_background);
#if GRAPHICS_TFT_COLORING_ENABLED
    int statusLeftEndX = 0;
    int statusRightStartX = screenW;
    const bool isClockHeader = transparent_background && show_date && (!titleStr || titleStr[0] == '\0');
    const auto activeThemeId = getActiveTheme().id;
    const bool useClockHeaderAccent = isClockHeader && useClockHeaderAccentTheme(activeThemeId);
#endif

    {
        const uint16_t headerColor = getThemeHeaderBg();
        const uint16_t headerTextColor = getThemeHeaderText();
        const uint16_t headerTitleColorForRole = use_title_color_override ? title_color_override : headerTextColor;
        uint16_t headerStatusColor = getThemeHeaderStatus();
#if GRAPHICS_TFT_COLORING_ENABLED
        // Clock frame uses transparent header + date + empty title.
        // For accent clock themes (Pink/Creamsicle + classic monochrome), tint
        // status items (battery outline, %, date, mail icon) to the header accent.
        if (useClockHeaderAccent) {
            headerStatusColor = getThemeHeaderBg();
        }

        if (transparent_background) {
            // Transparent clock headers should inherit whatever body off-color is
            // already active under the header (important for light/inverted themes).
            const uint16_t transparentBgColor = resolveTFTOffColorAt(0, headerHeight + 1, getThemeBodyBg());
            setAndRegisterTFTColorRole(TFTColorRole::HeaderBackground, transparentBgColor, transparentBgColor, 0, 0, screenW,
                                       headerHeight);
            setTFTColorRole(TFTColorRole::HeaderTitle, headerTitleColorForRole, transparentBgColor);
            setTFTColorRole(TFTColorRole::HeaderStatus, headerStatusColor, transparentBgColor);
        } else if (useInvertedHeaderStyle) {
            setAndRegisterTFTColorRole(TFTColorRole::HeaderBackground, headerColor, TFTPalette::Black, 0, 0, screenW,
                                       headerHeight);
            setTFTColorRole(TFTColorRole::HeaderTitle, headerColor, headerTitleColorForRole);
            setTFTColorRole(TFTColorRole::HeaderStatus, headerColor, headerStatusColor);
        } else {
            setAndRegisterTFTColorRole(TFTColorRole::HeaderBackground, TFTPalette::Black, headerColor, 0, 0, screenW,
                                       headerHeight);
            setTFTColorRole(TFTColorRole::HeaderTitle, headerTitleColorForRole, headerColor);
            setTFTColorRole(TFTColorRole::HeaderStatus, headerStatusColor, headerColor);
        }
#endif

        // === Inverted Header Background ===
        if (useInvertedHeaderStyle) {
            display->setColor(BLACK);
            display->fillRect(0, 0, screenW, headerHeight);
            display->setColor(WHITE);
            drawRoundedHighlight(display, x, y, screenW, highlightHeight, 2);
            display->setColor(BLACK);
        } else {
            display->setColor(BLACK);
            display->fillRect(0, 0, screenW, headerHeight);
// Keep the legacy white separator for monochrome displays only when header background is visible.
#if !GRAPHICS_TFT_COLORING_ENABLED
            if (!transparent_background) {
                display->setColor(WHITE);
                if (currentResolution == ScreenResolution::High) {
                    display->drawLine(0, 20, screenW, 20);
                } else {
                    display->drawLine(0, 14, screenW, 14);
                }
            }
#endif
        }

        if (transparent_background) {
            display->setColor(WHITE);
        }

#if GRAPHICS_TFT_COLORING_ENABLED
        // TFT role coloring expects foreground glyph bits to be "set".
        display->setColor(WHITE);
#endif

        // === Screen Title ===
        const char *headerTitle = titleStr ? titleStr : "";
        const int titleWidth = UIRenderer::measureStringWithEmotes(display, headerTitle);
        const int titleX = (SCREEN_WIDTH - titleWidth) / 2;
#if GRAPHICS_TFT_COLORING_ENABLED
        const int titleRegionWidth = titleWidth + (config.display.heading_bold ? 3 : 2);
        registerTFTColorRegion(TFTColorRole::HeaderTitle, titleX - 1, y, titleRegionWidth, FONT_HEIGHT_SMALL);
#endif
        UIRenderer::drawStringWithEmotes(display, titleX, y, headerTitle, FONT_HEIGHT_SMALL, 1, config.display.heading_bold);
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

    bool useHorizontalBattery = (currentResolution == ScreenResolution::High && screenW >= screenH);
    const int textY = y + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    bool hasBatteryFillRegion = false;
    int16_t batteryFillRegionX = 0;
    int16_t batteryFillRegionY = 0;
    int16_t batteryFillRegionW = 0;
    int16_t batteryFillRegionH = 0;
#if GRAPHICS_TFT_COLORING_ENABLED
    uint16_t batteryFillColor = getThemeBatteryFillColor(chargePercent);
    if (useClockHeaderAccent) {
        batteryFillColor = getThemeHeaderBg();
    }
#endif

    int batteryX = 1;
    int batteryY = HEADER_OFFSET_Y + 1;
#if !defined(M5STACK_UNITC6L)
    // === Battery Icons ===
    if (usbPowered && !isCharging) { // This is a basic check to determine USB Powered is flagged but not charging
        batteryX += 1;
        batteryY += 2;
        if (currentResolution == ScreenResolution::High) {
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
#if GRAPHICS_TFT_COLORING_ENABLED
                if (fillWidth > 0) {
                    hasBatteryFillRegion = true;
                    batteryFillRegionX = batteryX + 1;
                    batteryFillRegionY = batteryY + 1;
                    batteryFillRegionW = fillWidth;
                    batteryFillRegionH = 11;
                }
#endif
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
#if GRAPHICS_TFT_COLORING_ENABLED
                if (fillHeight > 0) {
                    hasBatteryFillRegion = true;
                    batteryFillRegionX = batteryX + 1;
                    batteryFillRegionY = fillY + 10;
                    batteryFillRegionW = 5;
                    batteryFillRegionH = fillHeight;
                }
#endif
            }
            batteryX += 9; // Icon + 2 pixels
        }
    }
#if GRAPHICS_TFT_COLORING_ENABLED
    statusLeftEndX = batteryX + 2;
#endif

    if (chargePercent != 101) {
        // === Battery % Display ===
        char chargeStr[4];
        snprintf(chargeStr, sizeof(chargeStr), "%d", chargePercent);
        int chargeNumWidth = display->getStringWidth(chargeStr);
        const int percentWidth = display->getStringWidth("%");
        const int percentX = batteryX + chargeNumWidth - 1;
        display->drawString(batteryX, textY, chargeStr);
        display->drawString(percentX, textY, "%");
#if GRAPHICS_TFT_COLORING_ENABLED
        statusLeftEndX = percentX + percentWidth + 2;
#endif
        if (isBold) {
            display->drawString(batteryX + 1, textY, chargeStr);
            display->drawString(percentX + 1, textY, "%");
#if GRAPHICS_TFT_COLORING_ENABLED
            statusLeftEndX = percentX + percentWidth + 3;
#endif
        }
    }

    // === Time and Right-aligned Icons ===
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    char timeStr[10] = "--:--";                          // Fallback display
    int timeStrWidth = display->getStringWidth("12:34"); // Default alignment
    int timeX = screenW - xOffset - timeStrWidth + 4;

    if (rtc_sec > 0) {
        // === Build Time String ===
        int hour, minute, second;
        graphics::decomposeTime(rtc_sec, hour, minute, second);
        snprintf(timeStr, sizeof(timeStr), "%d:%02d", hour, minute);

        // === Build Date String ===
        char datetimeStr[25];
        UIRenderer::formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display, false);
        char dateLine[40];

        if (currentResolution == ScreenResolution::High) {
            snprintf(dateLine, sizeof(dateLine), "%s", datetimeStr);
        } else {
            if (hasUnreadMessage) {
                snprintf(dateLine, sizeof(dateLine), "%s", &datetimeStr[5]);
            } else {
                snprintf(dateLine, sizeof(dateLine), "%s", &datetimeStr[2]);
            }
        }

        if (config.display.use_12h_clock) {
            bool isPM = hour >= 12;
            hour %= 12;
            if (hour == 0)
                hour = 12;
            snprintf(timeStr, sizeof(timeStr), "%d:%02d%s", hour, minute, isPM ? "p" : "a");
        }

        if (show_date) {
            timeStrWidth = display->getStringWidth(dateLine);
        } else {
            timeStrWidth = display->getStringWidth(timeStr);
        }
        timeX = screenW - xOffset - timeStrWidth + 3;
#if GRAPHICS_TFT_COLORING_ENABLED
        statusRightStartX = timeX - (useHorizontalBattery ? 22 : 16);
#endif

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
                if (useInvertedHeaderStyle) {
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
                if (useInvertedHeaderStyle) {
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
        } else if (externalNotificationModule->getMute()) {
            if (currentResolution == ScreenResolution::High) {
                int iconX = iconRightEdge - mute_symbol_big_width;
                int iconY = textY + (FONT_HEIGHT_SMALL - mute_symbol_big_height) / 2;

                if (useInvertedHeaderStyle) {
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

                if (useInvertedHeaderStyle) {
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

        if (show_date) {
            // === Draw Date ===
            display->drawString(timeX, textY, dateLine);
            if (isBold)
                display->drawString(timeX - 1, textY, dateLine);
        } else {
            // === Draw Time ===
            display->drawString(timeX, textY, timeStr);
            if (isBold)
                display->drawString(timeX - 1, textY, timeStr);
        }

    } else {
        // === No Time Available: Mail/Mute Icon Moves to Far Right ===
        int iconRightEdge = screenW - xOffset;
#if GRAPHICS_TFT_COLORING_ENABLED
        statusRightStartX = screenW - (useHorizontalBattery ? 22 : 12);
#endif
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
        } else if (externalNotificationModule->getMute()) {
            if (currentResolution == ScreenResolution::High) {
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
#endif
#if GRAPHICS_TFT_COLORING_ENABLED
    registerTFTColorRegion(TFTColorRole::HeaderStatus, 0, 0, statusLeftEndX, headerHeight);
    if (statusRightStartX < screenW) {
        registerTFTColorRegion(TFTColorRole::HeaderStatus, statusRightStartX, 0, screenW - statusRightStartX, headerHeight);
    }
    if (hasBatteryFillRegion) {
        registerTFTColorRegionDirect(batteryFillRegionX, batteryFillRegionY, batteryFillRegionW, batteryFillRegionH,
                                     batteryFillColor, headerColorForRoles);
    }
#endif
    display->setColor(WHITE); // Reset for other UI
}

const int *getTextPositions(OLEDDisplay *display)
{
    static int textPositions[7]; // Static array that persists beyond function scope

    if (currentResolution == ScreenResolution::High) {
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

// *************************
// * Common Footer Drawing *
// *************************
void drawCommonFooter(OLEDDisplay *display, int16_t x, int16_t y)
{
    if (!isAPIConnected(service->api_state))
        return;

    const int scale = (currentResolution == ScreenResolution::High) ? 2 : 1;
    const int footerY = SCREEN_HEIGHT - (1 * scale) - (connection_icon_height * scale);
    const int footerH = (connection_icon_height * scale) + (2 * scale);
    const int iconX = 0;
    const int iconY = SCREEN_HEIGHT - (connection_icon_height * scale);
    const int iconW = connection_icon_width * scale;
    const int iconH = connection_icon_height * scale;

#if GRAPHICS_TFT_COLORING_ENABLED
    // Only tint the link glyph itself on TFT; keep the footer background black.
    setAndRegisterTFTColorRole(TFTColorRole::ConnectionIcon, TFTPalette::Blue, TFTPalette::Black, iconX, iconY, iconW, iconH);
#endif

    display->setColor(BLACK);
    display->fillRect(0, footerY, SCREEN_WIDTH, footerH);
    display->setColor(WHITE);
    if (currentResolution == ScreenResolution::High) {
        const int bytesPerRow = (connection_icon_width + 7) / 8;

        for (int yy = 0; yy < connection_icon_height; ++yy) {
            const uint8_t *rowPtr = connection_icon + yy * bytesPerRow;
            for (int xx = 0; xx < connection_icon_width; ++xx) {
                const uint8_t byteVal = pgm_read_byte(rowPtr + (xx >> 3));
                const uint8_t bitMask = 1U << (xx & 7); // XBM is LSB-first
                if (byteVal & bitMask) {
                    display->fillRect(iconX + xx * scale, iconY + yy * scale, scale, scale);
                }
            }
        }

    } else {
        display->drawXbm(iconX, iconY, connection_icon_width, connection_icon_height, connection_icon);
    }
}

bool isAllowedPunctuation(char c)
{
    switch (c) {
    case '.':
    case ',':
    case '!':
    case '?':
    case ';':
    case ':':
    case '-':
    case '_':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '\'':
    case '"':
    case '@':
    case '#':
    case '$':
    case '/':
    case '\\':
    case '&':
    case '+':
    case '=':
    case '%':
    case '~':
    case '^':
    case ' ':
        return true;
    default:
        return false;
    }
}

static inline size_t utf8CodePointLength(unsigned char lead)
{
    if ((lead & 0x80) == 0x00) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

std::string sanitizeString(const std::string &input)
{
    static constexpr char kReplacementChar = static_cast<char>(0xBF); // Inverted question mark in ISO-8859-1.
    std::string output;
    output.reserve(input.size());
    bool inReplacement = false;
    const size_t inputSize = input.size();
    size_t i = 0;
    while (i < inputSize) {
        const unsigned char byte0 = static_cast<unsigned char>(input[i]);
        char normalized = '\0';
        size_t consumed = 0;
        if (byte0 < 0x80) {
            normalized = static_cast<char>(byte0);
            consumed = 1;
        } else if ((i + 2) < inputSize && byte0 == 0xE2 && static_cast<unsigned char>(input[i + 1]) == 0x80) {
            // Smart punctuation: ' ' \" \" - -
            switch (static_cast<unsigned char>(input[i + 2])) {
            case 0x98:
            case 0x99:
                normalized = '\'';
                consumed = 3;
                break;
            case 0x9C:
            case 0x9D:
                normalized = '\"';
                consumed = 3;
                break;
            case 0x93:
            case 0x94:
                normalized = '-';
                consumed = 3;
                break;
            default:
                break;
            }
        } else if ((i + 1) < inputSize && byte0 == 0xC2 && static_cast<unsigned char>(input[i + 1]) == 0xA0) {
            // Non-breaking space.
            normalized = ' ';
            consumed = 2;
        }
        if (consumed == 0) {
            size_t seqLen = utf8CodePointLength(byte0);
            if (seqLen > (inputSize - i)) {
                seqLen = 1;
            }
            if (!inReplacement) {
                output.push_back(kReplacementChar);
                inReplacement = true;
            }
            i += seqLen;
            continue;
        }
        const unsigned char normalizedUc = static_cast<unsigned char>(normalized);
        if (std::isalnum(normalizedUc) || isAllowedPunctuation(normalized)) {
            output.push_back(normalized);
            inReplacement = false;
        } else if (!inReplacement) {
            output.push_back(kReplacementChar);
            inReplacement = true;
        }
        i += consumed;
    }
    return output;
}

} // namespace graphics
#endif
