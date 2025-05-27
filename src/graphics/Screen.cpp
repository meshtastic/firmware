/*
BaseUI

Developed and Maintained By:
- Ronald Garcia (HarukiToreda) – Lead development and implementation.
- JasonP (Xaositek)  – Screen layout and icon design, UI improvements and testing.
- TonyG (Tropho) – Project management, structural planning, and testing

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.h"
#include "PowerMon.h"
#include "Throttle.h"
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>

#include "DisplayFormatters.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "ButtonThread.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "error.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "graphics/emotes.h"
#include "input/ScanAndSelect.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "RadioLibInterface.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "meshUtils.h"
#include "modules/AdminModule.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "modules/WaypointModule.h"
#include "sleep.h"
#include "target_specific.h"


using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#include "modules/StoreForwardModule.h"
#endif

#if ARCH_PORTDUINO
#include "modules/StoreForwardModule.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

using namespace meshtastic; /** @todo remove */

String alertBannerMessage = "";
uint32_t alertBannerUntil = 0;

namespace graphics
{

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS

// A text message frame + debug frame + all the node infos
FrameCallback *normalFrames;
static uint32_t targetFramerate = IDLE_FRAMERATE;
static String alertBannerMessage;
static uint32_t alertBannerUntil = 0;

uint32_t logo_timeout = 5000; // 4 seconds for EACH logo

uint32_t hours_in_month = 730;

// This image definition is here instead of images.h because it's modified dynamically by the drawBattery function
uint8_t imgBattery[16] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xE7, 0x3C};

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Stores the last 4 of our hardware ID, to make finding the device for pairing easier
static char ourId[5];

// vector where symbols (string) are displayed in bottom corner of display.
std::vector<std::string> functionSymbol;
// string displayed in bottom right corner of display. Created from elements in functionSymbol vector
std::string functionSymbolString = "";

#if HAS_GPS
// GeoCoord object for the screen
GeoCoord geoCoord;
#endif

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>


// Start Functions to write date/time to the screen
#include <string>  // Only needed if you're using std::string elsewhere

bool isLeapYear(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

const int daysInMonth[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

// Fills the buffer with a formatted date/time string and returns pixel width
int formatDateTime(char* buf, size_t bufSize, uint32_t rtc_sec, OLEDDisplay* display, bool includeTime) {
    int sec = rtc_sec % 60;
    rtc_sec /= 60;
    int min = rtc_sec % 60;
    rtc_sec /= 60;
    int hour = rtc_sec % 24;
    rtc_sec /= 24;

    int year = 1970;
    while (true) {
        int daysInYear = isLeapYear(year) ? 366 : 365;
        if (rtc_sec >= (uint32_t)daysInYear) {
            rtc_sec -= daysInYear;
            year++;
        } else {
            break;
        }
    }

    int month = 0;
    while (month < 12) {
        int dim = daysInMonth[month];
        if (month == 1 && isLeapYear(year)) dim++;
        if (rtc_sec >= (uint32_t)dim) {
            rtc_sec -= dim;
            month++;
        } else {
            break;
        }
    }

    int day = rtc_sec + 1;

    if (includeTime) {
        snprintf(buf, bufSize, "%04d-%02d-%02d %02d:%02d:%02d", year, month + 1, day, hour, min, sec);
    } else {
        snprintf(buf, bufSize, "%04d-%02d-%02d", year, month + 1, day);
    }

    return display->getStringWidth(buf);
}

// Usage: int stringWidth = formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display);
// End Functions to write date/time to the screen


void drawScaledXBitmap16x16(int x, int y, int width, int height, const uint8_t *bitmapXBM, OLEDDisplay *display)
{
    for (int row = 0; row < height; row++) {
        uint8_t rowMask = (1 << row);
        for (int col = 0; col < width; col++) {
            uint8_t colData = pgm_read_byte(&bitmapXBM[col]);
            if (colData & rowMask) {
                // Note: rows become X, columns become Y after transpose
                display->fillRect(x + row * 2, y + col * 2, 2, 2);
            }
        }
    }
}

#define getStringCenteredX(s) ((SCREEN_WIDTH - display->getStringWidth(s)) / 2)

// Check if the display can render a string (detect special chars; emoji)
static bool haveGlyphs(const char *str)
{
#if defined(OLED_PL) || defined(OLED_UA) || defined(OLED_RU) || defined(OLED_CS)
    // Don't want to make any assumptions about custom language support
    return true;
#endif

    // Check each character with the lookup function for the OLED library
    // We're not really meant to use this directly..
    bool have = true;
    for (uint16_t i = 0; i < strlen(str); i++) {
        uint8_t result = Screen::customFontTableLookup((uint8_t)str[i]);
        // If font doesn't support a character, it is substituted for ¿
        if (result == 191 && (uint8_t)str[i] != 191) {
            have = false;
            break;
        }
    }

    // LOG_DEBUG("haveGlyphs=%d", have);
    return have;
}
extern bool hasUnreadMessage;
/**
 * Draw the icon with extra info printed around the corners
 */
static void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *label = "BaseUI";
    display->setFont(FONT_SMALL);
    int textWidth = display->getStringWidth(label);
    int r = 3; // corner radius

    if (SCREEN_WIDTH > 128) {
        // === ORIGINAL WIDE SCREEN LAYOUT (unchanged) ===
        int padding = 4;
        int boxWidth = max(icon_width, textWidth) + (padding * 2) + 16;
        int boxHeight = icon_height + FONT_HEIGHT_SMALL + (padding * 3) - 8;
        int boxX = x - 1 + (SCREEN_WIDTH - boxWidth) / 2;
        int boxY = y - 6 + (SCREEN_HEIGHT - boxHeight) / 2;

        display->setColor(WHITE);
        display->fillRect(boxX + r, boxY, boxWidth - 2 * r, boxHeight);
        display->fillRect(boxX, boxY + r, boxWidth - 1, boxHeight - 2 * r);
        display->fillCircle(boxX + r, boxY + r, r);                                // Upper Left
        display->fillCircle(boxX + boxWidth - r - 1, boxY + r, r);                 // Upper Right
        display->fillCircle(boxX + r, boxY + boxHeight - r - 1, r);                // Lower Left
        display->fillCircle(boxX + boxWidth - r - 1, boxY + boxHeight - r - 1, r); // Lower Right

        display->setColor(BLACK);
        int iconX = boxX + (boxWidth - icon_width) / 2;
        int iconY = boxY + padding - 2;
        display->drawXbm(iconX, iconY, icon_width, icon_height, icon_bits);

        int labelY = iconY + icon_height + padding;
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH / 2 - 3, labelY, label);
        display->drawString(x + SCREEN_WIDTH / 2 - 2, labelY, label); // faux bold

    } else {
        // === TIGHT SMALL SCREEN LAYOUT ===
        int iconY = y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2;
        iconY -= 4;

        int labelY = iconY + icon_height - 2;

        int boxWidth = max(icon_width, textWidth) + 4;
        int boxX = x + (SCREEN_WIDTH - boxWidth) / 2;
        int boxY = iconY - 1;
        int boxBottom = labelY + FONT_HEIGHT_SMALL - 2;
        int boxHeight = boxBottom - boxY;

        display->setColor(WHITE);
        display->fillRect(boxX + r, boxY, boxWidth - 2 * r, boxHeight);
        display->fillRect(boxX, boxY + r, boxWidth - 1, boxHeight - 2 * r);
        display->fillCircle(boxX + r, boxY + r, r);
        display->fillCircle(boxX + boxWidth - r - 1, boxY + r, r);
        display->fillCircle(boxX + r, boxY + boxHeight - r - 1, r);
        display->fillCircle(boxX + boxWidth - r - 1, boxY + boxHeight - r - 1, r);

        display->setColor(BLACK);
        int iconX = boxX + (boxWidth - icon_width) / 2;
        display->drawXbm(iconX, iconY, icon_width, icon_height, icon_bits);

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH / 2, labelY, label);
    }

    // === Footer and headers (shared) ===
    display->setFont(FONT_MEDIUM);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = "meshtastic.org";
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);

    display->setFont(FONT_SMALL);
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    char buf[25];
    snprintf(buf, sizeof(buf), "%s\n%s", xstr(APP_VERSION_SHORT), haveGlyphs(owner.short_name) ? owner.short_name : "");
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + SCREEN_WIDTH, y + 0, buf);

    screen->forceDisplay();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

#ifdef USERPREFS_OEM_TEXT

static void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    static const uint8_t xbm[] = USERPREFS_OEM_IMAGE_DATA;
    display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                     y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2, USERPREFS_OEM_IMAGE_WIDTH,
                     USERPREFS_OEM_IMAGE_HEIGHT, xbm);

    switch (USERPREFS_OEM_FONT_SIZE) {
    case 0:
        display->setFont(FONT_SMALL);
        break;
    case 2:
        display->setFont(FONT_LARGE);
        break;
    default:
        display->setFont(FONT_MEDIUM);
        break;
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = USERPREFS_OEM_TEXT;
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version and shortname in upper right
    char buf[25];
    snprintf(buf, sizeof(buf), "%s\n%s", xstr(APP_VERSION_SHORT), haveGlyphs(owner.short_name) ? owner.short_name : "");

    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + SCREEN_WIDTH, y + 0, buf);
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
}

static void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

#endif

void Screen::drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *message)
{
    uint16_t x_offset = display->width() / 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, 26 + y, message);
}

// Used on boot when a certificate is being created
static void drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(64 + x, y, "Creating SSL certificate");

#ifdef ARCH_ESP32
    yield();
    esp_task_wdt_reset();
#endif

    display->setFont(FONT_SMALL);
    if ((millis() / 1000) % 2) {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . . .");
    } else {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . .  ");
    }
}

// Used when booting without a region set
static void drawWelcomeScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, y, "//\\ E S H T /\\ S T / C");
    display->drawString(64 + x, y + FONT_HEIGHT_SMALL, getDeviceName());
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if ((millis() / 10000) % 2) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 2 - 3, "Set the region using the");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 3 - 3, "Meshtastic Android, iOS,");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 4 - 3, "Web or CLI clients.");
    } else {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 2 - 3, "Visit meshtastic.org");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 3 - 3, "for more information.");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 4 - 3, "");
    }

#ifdef ARCH_ESP32
    yield();
    esp_task_wdt_reset();
#endif
}
// ==============================
// Overlay Alert Banner Renderer
// ==============================
// Displays a temporary centered banner message (e.g., warning, status, etc.)
// The banner appears in the center of the screen and disappears after the specified duration

// Called to trigger a banner with custom message and duration
void Screen::showOverlayBanner(const String &message, uint32_t durationMs)
{
    // Store the message and set the expiration timestamp
    alertBannerMessage = message;
    alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
}

// Draws the overlay banner on screen, if still within display duration
static void drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // Exit if no message is active or duration has passed
    if (alertBannerMessage.length() == 0 || (alertBannerUntil != 0 && millis() > alertBannerUntil))
        return;

    // === Layout Configuration ===
    constexpr uint16_t padding = 5;    // Padding around text inside the box
    constexpr uint8_t lineSpacing = 1; // Extra space between lines

    // Search the mesage to determine if we need the bell added
    bool needs_bell = (alertBannerMessage.indexOf("Alert Received") != -1);

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT); // We will manually center per line

    // === Split the message into lines (supports multi-line banners) ===
    std::vector<String> lines;
    int start = 0, newlineIdx;
    while ((newlineIdx = alertBannerMessage.indexOf('\n', start)) != -1) {
        lines.push_back(alertBannerMessage.substring(start, newlineIdx));
        start = newlineIdx + 1;
    }
    lines.push_back(alertBannerMessage.substring(start));

    // === Measure text dimensions ===
    uint16_t minWidth = (SCREEN_WIDTH > 128) ? 106 : 78;
    uint16_t maxWidth = 0;
    std::vector<uint16_t> lineWidths;
    for (const auto &line : lines) {
        uint16_t w = display->getStringWidth(line.c_str(), line.length(), true);
        lineWidths.push_back(w);
        if (w > maxWidth)
            maxWidth = w;
    }

    uint16_t boxWidth = padding * 2 + maxWidth;
    if (needs_bell && boxWidth < minWidth)
        boxWidth += (SCREEN_WIDTH > 128) ? 26 : 20;

    uint16_t boxHeight = padding * 2 + lines.size() * FONT_HEIGHT_SMALL + (lines.size() - 1) * lineSpacing;

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);

    // === Draw background box ===
    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Slightly oversized box
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight); // Border

    // === Draw each line centered in the box ===
    int16_t lineY = boxTop + padding;
    for (size_t i = 0; i < lines.size(); ++i) {
        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;
        uint16_t line_width = display->getStringWidth(lines[i].c_str(), lines[i].length(), true);

        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + line_width + 2, bellY, 8, 8, bell_alert);
        }

        display->drawString(textX, lineY, lines[i]);
        if (SCREEN_WIDTH > 128)
            display->drawString(textX + 1, lineY, lines[i]); // Faux bold

        lineY += FONT_HEIGHT_SMALL + lineSpacing;
    }
}

// draw overlay in bottom right corner of screen to show when notifications are muted or modifier key is active
static void drawFunctionOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // LOG_DEBUG("Draw function overlay");
    if (functionSymbol.begin() != functionSymbol.end()) {
        char buf[64];
        display->setFont(FONT_SMALL);
        snprintf(buf, sizeof(buf), "%s", functionSymbolString.c_str());
        display->drawString(SCREEN_WIDTH - display->getStringWidth(buf), SCREEN_HEIGHT - FONT_HEIGHT_SMALL, buf);
    }
}

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
static void drawDeepSleepScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Draw deep sleep screen");

    // Display displayStr on the screen
    drawIconScreen("Sleeping", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
static void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Draw screensaver overlay");

    EINK_ADD_FRAMEFLAG(display, COSMETIC); // Take the opportunity for a full-refresh

    // Config
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *pauseText = "Screen Paused";
    const char *idText = owner.short_name;
    const bool useId = haveGlyphs(idText); // This bool is used to hide the idText box if we can't render the short name
    constexpr uint16_t padding = 5;
    constexpr uint8_t dividerGap = 1;
    constexpr uint8_t imprecision = 5; // How far the box origins can drift from center. Combat burn-in.

    // Dimensions
    const uint16_t idTextWidth = display->getStringWidth(idText, strlen(idText), true); // "true": handle utf8 chars
    const uint16_t pauseTextWidth = display->getStringWidth(pauseText, strlen(pauseText));
    const uint16_t boxWidth = padding + (useId ? idTextWidth + padding + padding : 0) + pauseTextWidth + padding;
    const uint16_t boxHeight = padding + FONT_HEIGHT_SMALL + padding;

    // Position
    const int16_t boxLeft = (display->width() / 2) - (boxWidth / 2) + random(-imprecision, imprecision + 1);
    // const int16_t boxRight = boxLeft + boxWidth - 1;
    const int16_t boxTop = (display->height() / 2) - (boxHeight / 2 + random(-imprecision, imprecision + 1));
    const int16_t boxBottom = boxTop + boxHeight - 1;
    const int16_t idTextLeft = boxLeft + padding;
    const int16_t idTextTop = boxTop + padding;
    const int16_t pauseTextLeft = boxLeft + (useId ? padding + idTextWidth + padding : 0) + padding;
    const int16_t pauseTextTop = boxTop + padding;
    const int16_t dividerX = boxLeft + padding + idTextWidth + padding;
    const int16_t dividerTop = boxTop + 1 + dividerGap;
    const int16_t dividerBottom = boxBottom - 1 - dividerGap;

    // Draw: box
    display->setColor(EINK_WHITE);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Clear a slightly oversized area for the box
    display->setColor(EINK_BLACK);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

    // Draw: Text
    if (useId)
        display->drawString(idTextLeft, idTextTop, idText);
    display->drawString(pauseTextLeft, pauseTextTop, pauseText);
    display->drawString(pauseTextLeft + 1, pauseTextTop, pauseText); // Faux bold

    // Draw: divider
    if (useId)
        display->drawLine(dividerX, dividerTop, dividerX, dividerBottom);
}
#endif

static void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t module_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == TransitionRelationship_INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // LOG_DEBUG("Screen is not in transition.  Frame: %d", module_frame);
    }
    // LOG_DEBUG("Draw Module Frame %d", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Updating");

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawStringMaxWidth(0 + x, 2 + y + FONT_HEIGHT_SMALL * 2, x + display->getWidth(),
                                "Please be patient and do not power off.");
}

/// Draw the last text message we received
static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);

    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "Critical fault #%d", error_code);
    display->drawString(0 + x, 0 + y, tempBuf);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(0 + x, FONT_HEIGHT_MEDIUM + y, "For help, please visit \nmeshtastic.org");
}

// Ignore messages originating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const meshtastic_MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.store_forward.enabled;
}

// Draw power bars or a charging indicator on an image of a battery, determined by battery charge voltage or percentage.
static void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const PowerStatus *powerStatus)
{
    static const uint8_t powerBar[3] = {0x81, 0xBD, 0xBD};
    static const uint8_t lightning[8] = {0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85};

    // Clear the bar area inside the battery image
    for (int i = 1; i < 14; i++) {
        imgBuffer[i] = 0x81;
    }

    // Fill with lightning or power bars
    if (powerStatus->getIsCharging()) {
        memcpy(imgBuffer + 3, lightning, 8);
    } else {
        for (int i = 0; i < 4; i++) {
            if (powerStatus->getBatteryChargePercent() >= 25 * i)
                memcpy(imgBuffer + 1 + (i * 3), powerBar, 3);
        }
    }

    // Slightly more conservative scaling based on screen width
    int scale = 1;

    if (SCREEN_WIDTH >= 200)
        scale = 2;
    if (SCREEN_WIDTH >= 300)
        scale = 2; // Do NOT go higher than 2

    // Draw scaled battery image (16 columns × 8 rows)
    for (int col = 0; col < 16; col++) {
        uint8_t colBits = imgBuffer[col];
        for (int row = 0; row < 8; row++) {
            if (colBits & (1 << row)) {
                display->fillRect(x + col * scale, y + row * scale, scale, scale);
            }
        }
    }
}

#if defined(DISPLAY_CLOCK_FRAME)

void Screen::drawWatchFaceToggleButton(OLEDDisplay *display, int16_t x, int16_t y, bool digitalMode, float scale)
{
    uint16_t segmentWidth = SEGMENT_WIDTH * scale;
    uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

    if (digitalMode) {
        uint16_t radius = (segmentWidth + (segmentHeight * 2) + 4) / 2;
        uint16_t centerX = (x + segmentHeight + 2) + (radius / 2);
        uint16_t centerY = (y + segmentHeight + 2) + (radius / 2);

        display->drawCircle(centerX, centerY, radius);
        display->drawCircle(centerX, centerY, radius + 1);
        display->drawLine(centerX, centerY, centerX, centerY - radius + 3);
        display->drawLine(centerX, centerY, centerX + radius - 3, centerY);
    } else {
        uint16_t segmentOneX = x + segmentHeight + 2;
        uint16_t segmentOneY = y;

        uint16_t segmentTwoX = segmentOneX + segmentWidth + 2;
        uint16_t segmentTwoY = segmentOneY + segmentHeight + 2;

        uint16_t segmentThreeX = segmentOneX;
        uint16_t segmentThreeY = segmentTwoY + segmentWidth + 2;

        uint16_t segmentFourX = x;
        uint16_t segmentFourY = y + segmentHeight + 2;

        drawHorizontalSegment(display, segmentOneX, segmentOneY, segmentWidth, segmentHeight);
        drawVerticalSegment(display, segmentTwoX, segmentTwoY, segmentWidth, segmentHeight);
        drawHorizontalSegment(display, segmentThreeX, segmentThreeY, segmentWidth, segmentHeight);
        drawVerticalSegment(display, segmentFourX, segmentFourY, segmentWidth, segmentHeight);
    }
}

// Draw a digital clock
void Screen::drawDigitalClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawBattery(display, x, y + 7, imgBattery, powerStatus);

    if (powerStatus->getHasBattery()) {
        String batteryPercent = String(powerStatus->getBatteryChargePercent()) + "%";

        display->setFont(FONT_SMALL);

        display->drawString(x + 20, y + 2, batteryPercent);
    }

    if (nimbleBluetooth && nimbleBluetooth->isConnected()) {
        drawBluetoothConnectedIcon(display, display->getWidth() - 18, y + 2);
    }

    drawWatchFaceToggleButton(display, display->getWidth() - 36, display->getHeight() - 36, screen->digitalWatchFace, 1);

    display->setColor(OLEDDISPLAY_COLOR::WHITE);

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        int hour = hms / SEC_PER_HOUR;
        int minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int second = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        hour = hour > 12 ? hour - 12 : hour;

        if (hour == 0) {
            hour = 12;
        }

        // hours string
        String hourString = String(hour);

        // minutes string
        String minuteString = minute < 10 ? "0" + String(minute) : String(minute);

        String timeString = hourString + ":" + minuteString;

        // seconds string
        String secondString = second < 10 ? "0" + String(second) : String(second);

        float scale = 1.5;

        uint16_t segmentWidth = SEGMENT_WIDTH * scale;
        uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

        // calculate hours:minutes string width
        uint16_t timeStringWidth = timeString.length() * 5;

        for (uint8_t i = 0; i < timeString.length(); i++) {
            String character = String(timeString[i]);

            if (character == ":") {
                timeStringWidth += segmentHeight;
            } else {
                timeStringWidth += segmentWidth + (segmentHeight * 2) + 4;
            }
        }

        // calculate seconds string width
        uint16_t secondStringWidth = (secondString.length() * 12) + 4;

        // sum these to get total string width
        uint16_t totalWidth = timeStringWidth + secondStringWidth;

        uint16_t hourMinuteTextX = (display->getWidth() / 2) - (totalWidth / 2);

        uint16_t startingHourMinuteTextX = hourMinuteTextX;

        uint16_t hourMinuteTextY = (display->getHeight() / 2) - (((segmentWidth * 2) + (segmentHeight * 3) + 8) / 2);

        // iterate over characters in hours:minutes string and draw segmented characters
        for (uint8_t i = 0; i < timeString.length(); i++) {
            String character = String(timeString[i]);

            if (character == ":") {
                drawSegmentedDisplayColon(display, hourMinuteTextX, hourMinuteTextY, scale);

                hourMinuteTextX += segmentHeight + 6;
            } else {
                drawSegmentedDisplayCharacter(display, hourMinuteTextX, hourMinuteTextY, character.toInt(), scale);

                hourMinuteTextX += segmentWidth + (segmentHeight * 2) + 4;
            }

            hourMinuteTextX += 5;
        }

        // draw seconds string
        display->setFont(FONT_MEDIUM);
        display->drawString(startingHourMinuteTextX + timeStringWidth + 4,
                            (display->getHeight() - hourMinuteTextY) - FONT_HEIGHT_MEDIUM + 6, secondString);
    }
}

void Screen::drawSegmentedDisplayColon(OLEDDisplay *display, int x, int y, float scale)
{
    uint16_t segmentWidth = SEGMENT_WIDTH * scale;
    uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

    uint16_t cellHeight = (segmentWidth * 2) + (segmentHeight * 3) + 8;

    uint16_t topAndBottomX = x + (4 * scale);

    uint16_t quarterCellHeight = cellHeight / 4;

    uint16_t topY = y + quarterCellHeight;
    uint16_t bottomY = y + (quarterCellHeight * 3);

    display->fillRect(topAndBottomX, topY, segmentHeight, segmentHeight);
    display->fillRect(topAndBottomX, bottomY, segmentHeight, segmentHeight);
}

void Screen::drawSegmentedDisplayCharacter(OLEDDisplay *display, int x, int y, uint8_t number, float scale)
{
    // the numbers 0-9, each expressed as an array of seven boolean (0|1) values encoding the on/off state of
    // segment {innerIndex + 1}
    // e.g., to display the numeral '0', segments 1-6 are on, and segment 7 is off.
    uint8_t numbers[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, // 0          Display segment key
        {0, 1, 1, 0, 0, 0, 0}, // 1                   1
        {1, 1, 0, 1, 1, 0, 1}, // 2                  ___
        {1, 1, 1, 1, 0, 0, 1}, // 3              6  |   | 2
        {0, 1, 1, 0, 0, 1, 1}, // 4                 |_7̲_|
        {1, 0, 1, 1, 0, 1, 1}, // 5              5  |   | 3
        {1, 0, 1, 1, 1, 1, 1}, // 6                 |___|
        {1, 1, 1, 0, 0, 1, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8                   4
        {1, 1, 1, 1, 0, 1, 1}, // 9
    };

    // the width and height of each segment's central rectangle:
    //             _____________________
    //           ⋰|  (only this part,  |⋱
    //         ⋰  |   not including    |  ⋱
    //         ⋱  |   the triangles    |  ⋰
    //           ⋱|    on the ends)    |⋰
    //             ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾

    uint16_t segmentWidth = SEGMENT_WIDTH * scale;
    uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

    // segment x and y coordinates
    uint16_t segmentOneX = x + segmentHeight + 2;
    uint16_t segmentOneY = y;

    uint16_t segmentTwoX = segmentOneX + segmentWidth + 2;
    uint16_t segmentTwoY = segmentOneY + segmentHeight + 2;

    uint16_t segmentThreeX = segmentTwoX;
    uint16_t segmentThreeY = segmentTwoY + segmentWidth + 2 + segmentHeight + 2;

    uint16_t segmentFourX = segmentOneX;
    uint16_t segmentFourY = segmentThreeY + segmentWidth + 2;

    uint16_t segmentFiveX = x;
    uint16_t segmentFiveY = segmentThreeY;

    uint16_t segmentSixX = x;
    uint16_t segmentSixY = segmentTwoY;

    uint16_t segmentSevenX = segmentOneX;
    uint16_t segmentSevenY = segmentTwoY + segmentWidth + 2;

    if (numbers[number][0]) {
        drawHorizontalSegment(display, segmentOneX, segmentOneY, segmentWidth, segmentHeight);
    }

    if (numbers[number][1]) {
        drawVerticalSegment(display, segmentTwoX, segmentTwoY, segmentWidth, segmentHeight);
    }

    if (numbers[number][2]) {
        drawVerticalSegment(display, segmentThreeX, segmentThreeY, segmentWidth, segmentHeight);
    }

    if (numbers[number][3]) {
        drawHorizontalSegment(display, segmentFourX, segmentFourY, segmentWidth, segmentHeight);
    }

    if (numbers[number][4]) {
        drawVerticalSegment(display, segmentFiveX, segmentFiveY, segmentWidth, segmentHeight);
    }

    if (numbers[number][5]) {
        drawVerticalSegment(display, segmentSixX, segmentSixY, segmentWidth, segmentHeight);
    }

    if (numbers[number][6]) {
        drawHorizontalSegment(display, segmentSevenX, segmentSevenY, segmentWidth, segmentHeight);
    }
}

void Screen::drawHorizontalSegment(OLEDDisplay *display, int x, int y, int width, int height)
{
    int halfHeight = height / 2;

    // draw central rectangle
    display->fillRect(x, y, width, height);

    // draw end triangles
    display->fillTriangle(x, y, x, y + height - 1, x - halfHeight, y + halfHeight);

    display->fillTriangle(x + width, y, x + width + halfHeight, y + halfHeight, x + width, y + height - 1);
}

void Screen::drawVerticalSegment(OLEDDisplay *display, int x, int y, int width, int height)
{
    int halfHeight = height / 2;

    // draw central rectangle
    display->fillRect(x, y, height, width);

    // draw end triangles
    display->fillTriangle(x + halfHeight, y - halfHeight, x + height - 1, y, x, y);

    display->fillTriangle(x, y + width, x + height - 1, y + width, x + halfHeight, y + width + halfHeight);
}

void Screen::drawBluetoothConnectedIcon(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->drawFastImage(x, y, 18, 14, bluetoothConnectedIcon);
}

// Draw an analog clock
void Screen::drawAnalogClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawBattery(display, x, y + 7, imgBattery, powerStatus);

    if (powerStatus->getHasBattery()) {
        String batteryPercent = String(powerStatus->getBatteryChargePercent()) + "%";

        display->setFont(FONT_SMALL);

        display->drawString(x + 20, y + 2, batteryPercent);
    }

    if (nimbleBluetooth && nimbleBluetooth->isConnected()) {
        drawBluetoothConnectedIcon(display, display->getWidth() - 18, y + 2);
    }

    drawWatchFaceToggleButton(display, display->getWidth() - 36, display->getHeight() - 36, screen->digitalWatchFace, 1);

    // clock face center coordinates
    int16_t centerX = display->getWidth() / 2;
    int16_t centerY = display->getHeight() / 2;

    // clock face radius
    int16_t radius = (display->getWidth() / 2) * 0.8;

    // noon (0 deg) coordinates (outermost circle)
    int16_t noonX = centerX;
    int16_t noonY = centerY - radius;

    // second hand radius and y coordinate (outermost circle)
    int16_t secondHandNoonY = noonY + 1;

    // tick mark outer y coordinate; (first nested circle)
    int16_t tickMarkOuterNoonY = secondHandNoonY;

    // seconds tick mark inner y coordinate; (second nested circle)
    double secondsTickMarkInnerNoonY = (double)noonY + 8;

    // hours tick mark inner y coordinate; (third nested circle)
    double hoursTickMarkInnerNoonY = (double)noonY + 16;

    // minute hand y coordinate
    int16_t minuteHandNoonY = secondsTickMarkInnerNoonY + 4;

    // hour string y coordinate
    int16_t hourStringNoonY = minuteHandNoonY + 18;

    // hour hand radius and y coordinate
    int16_t hourHandRadius = radius * 0.55;
    int16_t hourHandNoonY = centerY - hourHandRadius;

    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    display->drawCircle(centerX, centerY, radius);

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int second = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        hour = hour > 12 ? hour - 12 : hour;

        int16_t degreesPerHour = 30;
        int16_t degreesPerMinuteOrSecond = 6;

        double hourBaseAngle = hour * degreesPerHour;
        double hourAngleOffset = ((double)minute / 60) * degreesPerHour;
        double hourAngle = radians(hourBaseAngle + hourAngleOffset);

        double minuteBaseAngle = minute * degreesPerMinuteOrSecond;
        double minuteAngleOffset = ((double)second / 60) * degreesPerMinuteOrSecond;
        double minuteAngle = radians(minuteBaseAngle + minuteAngleOffset);

        double secondAngle = radians(second * degreesPerMinuteOrSecond);

        double hourX = sin(-hourAngle) * (hourHandNoonY - centerY) + noonX;
        double hourY = cos(-hourAngle) * (hourHandNoonY - centerY) + centerY;

        double minuteX = sin(-minuteAngle) * (minuteHandNoonY - centerY) + noonX;
        double minuteY = cos(-minuteAngle) * (minuteHandNoonY - centerY) + centerY;

        double secondX = sin(-secondAngle) * (secondHandNoonY - centerY) + noonX;
        double secondY = cos(-secondAngle) * (secondHandNoonY - centerY) + centerY;

        display->setFont(FONT_MEDIUM);

        // draw minute and hour tick marks and hour numbers
        for (uint16_t angle = 0; angle < 360; angle += 6) {
            double angleInRadians = radians(angle);

            double sineAngleInRadians = sin(-angleInRadians);
            double cosineAngleInRadians = cos(-angleInRadians);

            double endX = sineAngleInRadians * (tickMarkOuterNoonY - centerY) + noonX;
            double endY = cosineAngleInRadians * (tickMarkOuterNoonY - centerY) + centerY;

            if (angle % degreesPerHour == 0) {
                double startX = sineAngleInRadians * (hoursTickMarkInnerNoonY - centerY) + noonX;
                double startY = cosineAngleInRadians * (hoursTickMarkInnerNoonY - centerY) + centerY;

                // draw hour tick mark
                display->drawLine(startX, startY, endX, endY);

                static char buffer[2];

                uint8_t hourInt = (angle / 30);

                if (hourInt == 0) {
                    hourInt = 12;
                }

                // hour number x offset needs to be adjusted for some cases
                int8_t hourStringXOffset;
                int8_t hourStringYOffset = 13;

                switch (hourInt) {
                case 3:
                    hourStringXOffset = 5;
                    break;
                case 9:
                    hourStringXOffset = 7;
                    break;
                case 10:
                case 11:
                    hourStringXOffset = 8;
                    break;
                case 12:
                    hourStringXOffset = 13;
                    break;
                default:
                    hourStringXOffset = 6;
                    break;
                }

                double hourStringX = (sineAngleInRadians * (hourStringNoonY - centerY) + noonX) - hourStringXOffset;
                double hourStringY = (cosineAngleInRadians * (hourStringNoonY - centerY) + centerY) - hourStringYOffset;

                // draw hour number
                display->drawStringf(hourStringX, hourStringY, buffer, "%d", hourInt);
            }

            if (angle % degreesPerMinuteOrSecond == 0) {
                double startX = sineAngleInRadians * (secondsTickMarkInnerNoonY - centerY) + noonX;
                double startY = cosineAngleInRadians * (secondsTickMarkInnerNoonY - centerY) + centerY;

                // draw minute tick mark
                display->drawLine(startX, startY, endX, endY);
            }
        }

        // draw hour hand
        display->drawLine(centerX, centerY, hourX, hourY);

        // draw minute hand
        display->drawLine(centerX, centerY, minuteX, minuteY);

        // draw second hand
        display->drawLine(centerX, centerY, secondX, secondY);
    }
}

#endif

// Get an absolute time from "seconds ago" info. Returns false if no valid timestamp possible
bool deltaToTimestamp(uint32_t secondsAgo, uint8_t *hours, uint8_t *minutes, int32_t *daysAgo)
{
    // Cache the result - avoid frequent recalculation
    static uint8_t hoursCached = 0, minutesCached = 0;
    static uint32_t daysAgoCached = 0;
    static uint32_t secondsAgoCached = 0;
    static bool validCached = false;

    // Abort: if timezone not set
    if (strlen(config.device.tzdef) == 0) {
        validCached = false;
        return validCached;
    }

    // Abort: if invalid pointers passed
    if (hours == nullptr || minutes == nullptr || daysAgo == nullptr) {
        validCached = false;
        return validCached;
    }

    // Abort: if time seems invalid.. (> 6 months ago, probably seen before RTC set)
    if (secondsAgo > SEC_PER_DAY * 30UL * 6) {
        validCached = false;
        return validCached;
    }

    // If repeated request, don't bother recalculating
    if (secondsAgo - secondsAgoCached < 60 && secondsAgoCached != 0) {
        if (validCached) {
            *hours = hoursCached;
            *minutes = minutesCached;
            *daysAgo = daysAgoCached;
        }
        return validCached;
    }

    // Get local time
    uint32_t secondsRTC = getValidTime(RTCQuality::RTCQualityDevice, true); // Get local time

    // Abort: if RTC not set
    if (!secondsRTC) {
        validCached = false;
        return validCached;
    }

    // Get absolute time when last seen
    uint32_t secondsSeenAt = secondsRTC - secondsAgo;

    // Calculate daysAgo
    *daysAgo = (secondsRTC / SEC_PER_DAY) - (secondsSeenAt / SEC_PER_DAY); // How many "midnights" have passed

    // Get seconds since midnight
    uint32_t hms = (secondsRTC - secondsAgo) % SEC_PER_DAY;
    hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

    // Tear apart hms into hours and minutes
    *hours = hms / SEC_PER_HOUR;
    *minutes = (hms % SEC_PER_HOUR) / SEC_PER_MIN;

    // Cache the result
    daysAgoCached = *daysAgo;
    hoursCached = *hours;
    minutesCached = *minutes;
    secondsAgoCached = secondsAgo;

    validCached = true;
    return validCached;
}

void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount)
{
    int cursorX = x;
    const int fontHeight = FONT_HEIGHT_SMALL;

    // === Step 1: Find tallest emote in the line ===
    int maxIconHeight = fontHeight;
    for (size_t i = 0; i < line.length();) {
        bool matched = false;
        for (int e = 0; e < emoteCount; ++e) {
            size_t emojiLen = strlen(emotes[e].label);
            if (line.compare(i, emojiLen, emotes[e].label) == 0) {
                if (emotes[e].height > maxIconHeight)
                    maxIconHeight = emotes[e].height;
                i += emojiLen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            uint8_t c = static_cast<uint8_t>(line[i]);
            if ((c & 0xE0) == 0xC0)
                i += 2;
            else if ((c & 0xF0) == 0xE0)
                i += 3;
            else if ((c & 0xF8) == 0xF0)
                i += 4;
            else
                i += 1;
        }
    }

    // === Step 2: Baseline alignment ===
    int lineHeight = std::max(fontHeight, maxIconHeight);
    int baselineOffset = (lineHeight - fontHeight) / 2;
    int fontY = y + baselineOffset;
    int fontMidline = fontY + fontHeight / 2;

    // === Step 3: Render line in segments ===
    size_t i = 0;
    bool inBold = false;

    while (i < line.length()) {
        // Check for ** start/end for faux bold
        if (line.compare(i, 2, "**") == 0) {
            inBold = !inBold;
            i += 2;
            continue;
        }

        // Look ahead for the next emote match
        size_t nextEmotePos = std::string::npos;
        const Emote *matchedEmote = nullptr;
        size_t emojiLen = 0;

        for (int e = 0; e < emoteCount; ++e) {
            size_t pos = line.find(emotes[e].label, i);
            if (pos != std::string::npos && (nextEmotePos == std::string::npos || pos < nextEmotePos)) {
                nextEmotePos = pos;
                matchedEmote = &emotes[e];
                emojiLen = strlen(emotes[e].label);
            }
        }

        // Render normal text segment up to the emote or bold toggle
        size_t nextControl = std::min(nextEmotePos, line.find("**", i));
        if (nextControl == std::string::npos)
            nextControl = line.length();

        if (nextControl > i) {
            std::string textChunk = line.substr(i, nextControl - i);
            if (inBold) {
                // Faux bold: draw twice, offset by 1px
                display->drawString(cursorX + 1, fontY, textChunk.c_str());
            }
            display->drawString(cursorX, fontY, textChunk.c_str());
            cursorX += display->getStringWidth(textChunk.c_str());
            i = nextControl;
            continue;
        }

        // Render the emote (if found)
        if (matchedEmote && i == nextEmotePos) {
            int iconY = fontMidline - matchedEmote->height / 2 - 1;
            display->drawXbm(cursorX, iconY, matchedEmote->width, matchedEmote->height, matchedEmote->bitmap);
            cursorX += matchedEmote->width + 1;
            i += emojiLen;
        } else {
            // No more emotes — render the rest of the line
            std::string remaining = line.substr(i);
            if (inBold) {
                display->drawString(cursorX + 1, fontY, remaining.c_str());
            }
            display->drawString(cursorX, fontY, remaining.c_str());
            cursorX += display->getStringWidth(remaining.c_str());
            break;
        }
    }
}

// ****************************
// *   Text Message Screen    *
// ****************************
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Clear the unread message indicator when viewing the message
    hasUnreadMessage = false;

    const meshtastic_MeshPacket &mp = devicestate.rx_text_message;
    const char *msg = reinterpret_cast<const char *>(mp.decoded.payload.bytes);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    const int navHeight = FONT_HEIGHT_SMALL;
    const int scrollBottom = SCREEN_HEIGHT - navHeight;
    const int usableHeight = scrollBottom;
    const int textWidth = SCREEN_WIDTH;
    const int cornerRadius = 2;

    bool isInverted = (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
    bool isBold = config.display.heading_bold;

    // === Header Construction ===
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(&mp));
    char headerStr[80];
    const char *sender = "???";
    if (node && node->has_user) {
        if (SCREEN_WIDTH >= 200 && strlen(node->user.long_name) > 0) {
            sender = node->user.long_name;
        } else {
            sender = node->user.short_name;
        }
    }
    uint32_t seconds = sinceReceived(&mp), minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
    uint8_t timestampHours, timestampMinutes;
    int32_t daysAgo;
    bool useTimestamp = deltaToTimestamp(seconds, &timestampHours, &timestampMinutes, &daysAgo);

    if (useTimestamp && minutes >= 15 && daysAgo == 0) {
        std::string prefix = (daysAgo == 1 && SCREEN_WIDTH >= 200) ? "Yesterday" : "At";
        if (config.display.use_12h_clock) {
            bool isPM = timestampHours >= 12;
            timestampHours = timestampHours % 12;
            if (timestampHours == 0)
                timestampHours = 12;
            snprintf(headerStr, sizeof(headerStr), "%s %d:%02d%s from %s", prefix.c_str(), timestampHours, timestampMinutes,
                     isPM ? "p" : "a", sender);
        } else {
            snprintf(headerStr, sizeof(headerStr), "%s %d:%02d from %s", prefix.c_str(), timestampHours, timestampMinutes,
                     sender);
        }
    } else {
        snprintf(headerStr, sizeof(headerStr), "%s ago from %s", screen->drawTimeDelta(days, hours, minutes, seconds).c_str(),
                 sender);
    }

#ifndef EXCLUDE_EMOJI
    // === Bounce animation setup ===
    static uint32_t lastBounceTime = 0;
    static int bounceY = 0;
    const int bounceRange = 2;     // Max pixels to bounce up/down
    const int bounceInterval = 60; // How quickly to change bounce direction (ms)

    uint32_t now = millis();
    if (now - lastBounceTime >= bounceInterval) {
        lastBounceTime = now;
        bounceY = (bounceY + 1) % (bounceRange * 2);
    }
    for (int i = 0; i < numEmotes; ++i) {
        const Emote &e = emotes[i];
        if (strcmp(msg, e.label) == 0){
            // Draw the header
            if (isInverted) {
                drawRoundedHighlight(display, x, 0, SCREEN_WIDTH, FONT_HEIGHT_SMALL - 1, cornerRadius);
                display->setColor(BLACK);
                display->drawString(x + 3, 0, headerStr);
                if (isBold)
                    display->drawString(x + 4, 0, headerStr);
                display->setColor(WHITE);
            } else {
                display->drawString(x, 0, headerStr);
                if (SCREEN_WIDTH > 128) {
                    display->drawLine(0, 20, SCREEN_WIDTH, 20);
                } else {
                    display->drawLine(0, 14, SCREEN_WIDTH, 14);
                }
            }

            // Center the emote below header + apply bounce
            int remainingHeight = SCREEN_HEIGHT - FONT_HEIGHT_SMALL - navHeight;
            int emoteY = FONT_HEIGHT_SMALL + (remainingHeight - e.height) / 2 + bounceY - bounceRange;
            display->drawXbm((SCREEN_WIDTH - e.width) / 2, emoteY, e.width, e.height, e.bitmap);
            return;
        }
    }
#endif

    // === Word-wrap and build line list ===
    char messageBuf[237];
    snprintf(messageBuf, sizeof(messageBuf), "%s", msg);

    std::vector<std::string> lines;
    lines.push_back(std::string(headerStr)); // Header line is always first

    std::string line, word;
    for (int i = 0; messageBuf[i]; ++i) {
        char ch = messageBuf[i];
        if (ch == '\n') {
            if (!word.empty())
                line += word;
            if (!line.empty())
                lines.push_back(line);
            line.clear();
            word.clear();
        } else if (ch == ' ') {
            line += word + ' ';
            word.clear();
        } else {
            word += ch;
            std::string test = line + word;
            if (display->getStringWidth(test.c_str()) > textWidth + 4) {
                if (!line.empty())
                    lines.push_back(line);
                line = word;
                word.clear();
            }
        }
    }
    if (!word.empty())
        line += word;
    if (!line.empty())
        lines.push_back(line);

    // === Scrolling logic ===
    std::vector<int> rowHeights;

    for (const auto &line : lines) {
        int maxHeight = FONT_HEIGHT_SMALL;
        for (int i = 0; i < numEmotes; ++i) {
            const Emote &e = emotes[i];
            if (line.find(e.label) != std::string::npos) {
                if (e.height > maxHeight)
                    maxHeight = e.height;
            }
        }
        rowHeights.push_back(maxHeight);
    }
    int totalHeight = 0;
    for (size_t i = 1; i < rowHeights.size(); ++i) {
        totalHeight += rowHeights[i];
    }
    int usableScrollHeight = usableHeight - rowHeights[0]; // remove header height
    int scrollStop = std::max(0, totalHeight - usableScrollHeight);

    static float scrollY = 0.0f;
    static uint32_t lastTime = 0, scrollStartDelay = 0, pauseStart = 0;
    static bool waitingToReset = false, scrollStarted = false;

    // === Smooth scrolling adjustment ===
    // You can tweak this divisor to change how smooth it scrolls.
    // Lower = smoother, but can feel slow.
    float delta = (now - lastTime) / 400.0f;
    lastTime = now;

    const float scrollSpeed = 2.0f; // pixels per second

    // Delay scrolling start by 2 seconds
    if (scrollStartDelay == 0)
        scrollStartDelay = now;
    if (!scrollStarted && now - scrollStartDelay > 2000)
        scrollStarted = true;

    if (totalHeight > usableHeight) {
        if (scrollStarted) {
            if (!waitingToReset) {
                scrollY += delta * scrollSpeed;
                if (scrollY >= scrollStop) {
                    scrollY = scrollStop;
                    waitingToReset = true;
                    pauseStart = lastTime;
                }
            } else if (lastTime - pauseStart > 3000) {
                scrollY = 0;
                waitingToReset = false;
                scrollStarted = false;
                scrollStartDelay = lastTime;
            }
        }
    } else {
        scrollY = 0;
    }

    int scrollOffset = static_cast<int>(scrollY);
    int yOffset = -scrollOffset;
    if (!isInverted) {
        if (SCREEN_WIDTH > 128) {
            display->drawLine(0, yOffset + 20, SCREEN_WIDTH, yOffset + 20);
        } else {
            display->drawLine(0, yOffset + 14, SCREEN_WIDTH, yOffset + 14);
        }
    }

    // === Render visible lines ===
    for (size_t i = 0; i < lines.size(); ++i) {
        int lineY = yOffset;
        for (size_t j = 0; j < i; ++j)
            lineY += rowHeights[j];
        if (lineY > -rowHeights[i] && lineY < scrollBottom) {
            if (i == 0 && isInverted) {
                drawRoundedHighlight(display, x, lineY, SCREEN_WIDTH, FONT_HEIGHT_SMALL - 1, cornerRadius);
                display->setColor(BLACK);
                display->drawString(x + 3, lineY, lines[i].c_str());
                if (isBold)
                    display->drawString(x + 4, lineY, lines[i].c_str());
                display->setColor(WHITE);
            } else {
                drawStringWithEmotes(display, x, lineY, lines[i], emotes, numEmotes);
            }
        }
    }
}

/// Draw a series of fields in a column, wrapping to multiple columns if needed
void Screen::drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
{
    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char **f = fields;
    int xo = x, yo = y;
    while (*f) {
        display->drawString(xo, yo, *f);
        if ((display->getColor() == BLACK) && config.display.heading_bold)
            display->drawString(xo + 1, yo, *f);

        display->setColor(WHITE);
        yo += FONT_HEIGHT_SMALL;
        if (yo > SCREEN_HEIGHT - FONT_HEIGHT_SMALL) {
            xo += SCREEN_WIDTH / 2;
            yo = 0;
        }
        f++;
    }
}

// Draw nodes status
static void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const NodeStatus *nodeStatus, int node_offset = 0,
                      bool show_total = true, String additional_words = "")
{
    char usersString[20];
    int nodes_online = (nodeStatus->getNumOnline() > 0) ? nodeStatus->getNumOnline() + node_offset : 0;

    snprintf(usersString, sizeof(usersString), "%d", nodes_online);

    if (show_total) {
        int nodes_total = (nodeStatus->getNumTotal() > 0) ? nodeStatus->getNumTotal() + node_offset : 0;
        snprintf(usersString, sizeof(usersString), "%d/%d", nodes_online, nodes_total);
    }

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS)) &&                                  \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
    display->drawFastImage(x, y + 3, 8, 8, imgUser);
#else
    display->drawFastImage(x, y + 1, 8, 8, imgUser);
#endif
    display->drawString(x + 10, y - 2, usersString);
    int string_offset = (SCREEN_WIDTH > 128) ? 2 : 1;
    if (additional_words != "") {
        display->drawString(x + 10 + display->getStringWidth(usersString) + string_offset, y - 2, additional_words);
        if (config.display.heading_bold)
            display->drawString(x + 11 + display->getStringWidth(usersString) + string_offset, y - 2, additional_words);
    }
}
#if HAS_GPS
// Draw GPS status summary
static void drawGPS(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        display->drawString(x - 1, y - 2, "Fixed GPS");
        if (config.display.heading_bold)
            display->drawString(x, y - 2, "Fixed GPS");
        return;
    }
    if (!gps->getIsConnected()) {
        display->drawString(x, y - 2, "No GPS");
        if (config.display.heading_bold)
            display->drawString(x + 1, y - 2, "No GPS");
        return;
    }
    // Adjust position if we’re going to draw too wide
    int maxDrawWidth = 6; // Position icon

    if (!gps->getHasLock()) {
        maxDrawWidth += display->getStringWidth("No sats") + 2; // icon + text + buffer
    } else {
        maxDrawWidth += (5 * 2) + 8 + display->getStringWidth("99") + 2; // bars + sat icon + text + buffer
    }

    if (x + maxDrawWidth > SCREEN_WIDTH) {
        x = SCREEN_WIDTH - maxDrawWidth;
        if (x < 0)
            x = 0; // Clamp to screen
    }

    display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gps->getHasLock()) {
        // Draw "No sats" to the right of the icon with slightly more gap
        int textX = x + 9; // 6 (icon) + 3px spacing
        display->drawString(textX, y - 3, "No sats");
        if (config.display.heading_bold)
            display->drawString(textX + 1, y - 3, "No sats");
        return;
    } else {
        char satsString[3];
        uint8_t bar[2] = {0};

        // Draw DOP signal bars
        for (int i = 0; i < 5; i++) {
            if (gps->getDOP() <= dopThresholds[i])
                bar[0] = ~((1 << (5 - i)) - 1);
            else
                bar[0] = 0b10000000;

            display->drawFastImage(x + 9 + (i * 2), y, 2, 8, bar);
        }

        // Draw satellite image
        display->drawFastImage(x + 24, y, 8, 8, imgSatellite);

        // Draw the number of satellites
        snprintf(satsString, sizeof(satsString), "%u", gps->getNumSatellites());
        int textX = x + 34;
        display->drawString(textX, y - 2, satsString);
        if (config.display.heading_bold)
            display->drawString(textX + 1, y - 2, satsString);
    }
}

// Draw status when GPS is disabled or not present
static void drawGPSpowerstat(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    String displayLine;
    int pos;
    if (y < FONT_HEIGHT_SMALL) { // Line 1: use short string
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        pos = SCREEN_WIDTH - display->getStringWidth(displayLine);
    } else {
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "GPS not present"
                                                                                                       : "GPS is disabled";
        pos = (SCREEN_WIDTH - display->getStringWidth(displayLine)) / 2;
    }
    display->drawString(x + pos, y, displayLine);
}

static void drawGPSAltitude(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    String displayLine = "";
    if (!gps->getIsConnected() && !config.position.fixed_position) {
        // displayLine = "No GPS Module";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        // displayLine = "No GPS Lock";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {
        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
        displayLine = "Altitude: " + String(geoCoord.getAltitude()) + "m";
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            displayLine = "Altitude: " + String(geoCoord.getAltitude() * METERS_TO_FEET) + "ft";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}

// Draw GPS status coordinates
static void drawGPScoordinates(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    auto gpsFormat = config.display.gps_format;
    String displayLine = "";

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        displayLine = "No GPS present";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        displayLine = "No GPS Lock";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {

        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));

        if (gpsFormat != meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DMS) {
            char coordinateLine[22];
            if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DEC) { // Decimal Degrees
                snprintf(coordinateLine, sizeof(coordinateLine), "%f %f", geoCoord.getLatitude() * 1e-7,
                         geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_UTM) { // Universal Transverse Mercator
                snprintf(coordinateLine, sizeof(coordinateLine), "%2i%1c %06u %07u", geoCoord.getUTMZone(), geoCoord.getUTMBand(),
                         geoCoord.getUTMEasting(), geoCoord.getUTMNorthing());
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_MGRS) { // Military Grid Reference System
                snprintf(coordinateLine, sizeof(coordinateLine), "%2i%1c %1c%1c %05u %05u", geoCoord.getMGRSZone(),
                         geoCoord.getMGRSBand(), geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k(),
                         geoCoord.getMGRSEasting(), geoCoord.getMGRSNorthing());
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_OLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine);
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_OSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') // OSGR is only valid around the UK region
                    snprintf(coordinateLine, sizeof(coordinateLine), "%s", "Out of Boundary");
                else
                    snprintf(coordinateLine, sizeof(coordinateLine), "%1c%1c %05u %05u", geoCoord.getOSGRE100k(),
                             geoCoord.getOSGRN100k(), geoCoord.getOSGREasting(), geoCoord.getOSGRNorthing());
            }

            // If fixed position, display text "Fixed GPS" alternating with the coordinates.
            if (config.position.fixed_position) {
                if ((millis() / 10000) % 2) {
                    display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(coordinateLine))) / 2, y, coordinateLine);
                } else {
                    display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth("Fixed GPS"))) / 2, y, "Fixed GPS");
                }
            } else {
                display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(coordinateLine))) / 2, y, coordinateLine);
            }
        } else {
            char latLine[22];
            char lonLine[22];
            snprintf(latLine, sizeof(latLine), "%2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(), geoCoord.getDMSLatMin(),
                     geoCoord.getDMSLatSec(), geoCoord.getDMSLatCP());
            snprintf(lonLine, sizeof(lonLine), "%3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(), geoCoord.getDMSLonMin(),
                     geoCoord.getDMSLonSec(), geoCoord.getDMSLonCP());
            display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(latLine))) / 2, y - FONT_HEIGHT_SMALL * 1, latLine);
            display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(lonLine))) / 2, y, lonLine);
        }
    }
}
#endif
/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
float Screen::estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0) {
        // just prepare for next time
        oldLat = lat;
        oldLon = lon;

        return b;
    }

    float d = GeoCoord::latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = GeoCoord::bearing(oldLat, oldLon, lat, lon);
    oldLat = lat;
    oldLon = lon;

    return b;
}

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static int8_t prevFrame = -1;

// Draw the arrow pointing to a node's location
void Screen::drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, uint16_t compassDiam, float headingRadian)
{
    Point tip(0.0f, 0.5f), tail(0.0f, -0.35f); // pointing up initially
    float arrowOffsetX = 0.14f, arrowOffsetY = 1.0f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y - arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y - arrowOffsetY);

    Point *arrowPoints[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++) {
        arrowPoints[i]->rotate(headingRadian);
        arrowPoints[i]->scale(compassDiam * 0.6);
        arrowPoints[i]->translate(compassX, compassY);
    }
    /* Old arrow
    display->drawLine(tip.x, tip.y, tail.x, tail.y);
    display->drawLine(leftArrow.x, leftArrow.y, tip.x, tip.y);
    display->drawLine(rightArrow.x, rightArrow.y, tip.x, tip.y);
    display->drawLine(leftArrow.x, leftArrow.y, tail.x, tail.y);
    display->drawLine(rightArrow.x, rightArrow.y, tail.x, tail.y);
    */
#ifdef USE_EINK
    display->drawTriangle(tip.x, tip.y, rightArrow.x, rightArrow.y, tail.x, tail.y);
#else
    display->fillTriangle(tip.x, tip.y, rightArrow.x, rightArrow.y, tail.x, tail.y);
#endif
    display->drawTriangle(tip.x, tip.y, leftArrow.x, leftArrow.y, tail.x, tail.y);
}

// Get a string representation of the time passed since something happened
void Screen::getTimeAgoStr(uint32_t agoSecs, char *timeStr, uint8_t maxLength)
{
    // Use an absolute timestamp in some cases.
    // Particularly useful with E-Ink displays. Static UI, fewer refreshes.
    uint8_t timestampHours, timestampMinutes;
    int32_t daysAgo;
    bool useTimestamp = deltaToTimestamp(agoSecs, &timestampHours, &timestampMinutes, &daysAgo);

    if (agoSecs < 120) // last 2 mins?
        snprintf(timeStr, maxLength, "%u seconds ago", agoSecs);
    // -- if suitable for timestamp --
    else if (useTimestamp && agoSecs < 15 * SECONDS_IN_MINUTE) // Last 15 minutes
        snprintf(timeStr, maxLength, "%u minutes ago", agoSecs / SECONDS_IN_MINUTE);
    else if (useTimestamp && daysAgo == 0) // Today
        snprintf(timeStr, maxLength, "Last seen: %02u:%02u", (unsigned int)timestampHours, (unsigned int)timestampMinutes);
    else if (useTimestamp && daysAgo == 1) // Yesterday
        snprintf(timeStr, maxLength, "Seen yesterday");
    else if (useTimestamp && daysAgo > 1) // Last six months (capped by deltaToTimestamp method)
        snprintf(timeStr, maxLength, "%li days ago", (long)daysAgo);
    // -- if using time delta instead --
    else if (agoSecs < 120 * 60) // last 2 hrs
        snprintf(timeStr, maxLength, "%u minutes ago", agoSecs / 60);
    // Only show hours ago if it's been less than 6 months. Otherwise, we may have bad data.
    else if ((agoSecs / 60 / 60) < (hours_in_month * 6))
        snprintf(timeStr, maxLength, "%u hours ago", agoSecs / 60 / 60);
    else
        snprintf(timeStr, maxLength, "unknown age");
}

void Screen::drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading)
{
    Serial.print("🧭 [Main Compass] Raw Heading (deg): ");
    Serial.println(myHeading * RAD_TO_DEG);

    // If north is supposed to be at the top of the compass we want rotation to be +0
    if (config.display.compass_north_top)
        myHeading = -0;
    /* N sign points currently not deleted*/
    Point N1(-0.04f, 0.65f), N2(0.04f, 0.65f); // N sign points (N1-N4)
    Point N3(-0.04f, 0.55f), N4(0.04f, 0.55f);
    Point NC1(0.00f, 0.50f); // north circle center point
    Point *rosePoints[] = {&N1, &N2, &N3, &N4, &NC1};

    uint16_t compassDiam = Screen::getCompassDiam(SCREEN_WIDTH, SCREEN_HEIGHT);

    for (int i = 0; i < 5; i++) {
        // North on compass will be negative of heading
        rosePoints[i]->rotate(-myHeading);
        rosePoints[i]->scale(compassDiam);
        rosePoints[i]->translate(compassX, compassY);
    }
}

uint16_t Screen::getCompassDiam(uint32_t displayWidth, uint32_t displayHeight)
{
    uint16_t diam = 0;
    uint16_t offset = 0;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT)
        offset = FONT_HEIGHT_SMALL;

    // get the smaller of the 2 dimensions and subtract 20
    if (displayWidth > (displayHeight - offset)) {
        diam = displayHeight - offset;
        // if 2/3 of the other size would be smaller, use that
        if (diam > (displayWidth * 2 / 3)) {
            diam = displayWidth * 2 / 3;
        }
    } else {
        diam = displayWidth;
        if (diam > ((displayHeight - offset) * 2 / 3)) {
            diam = (displayHeight - offset) * 2 / 3;
        }
    }

    return diam - 20;
};

// *********************
// *    Node Info      *
// *********************
static void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    static std::vector<meshtastic_NodeInfoLite *> favoritedNodes;
    static int prevFrame = -1;

    if (state->currentFrame != prevFrame) {
        prevFrame = state->currentFrame;

        favoritedNodes.clear();
        size_t total = nodeDB->getNumMeshNodes();
        for (size_t i = 0; i < total; i++) {
            meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            if (!n || n->num == nodeDB->getNodeNum()) continue;
            if (n->is_favorite) favoritedNodes.push_back(n);
        }

        // Sort favorites by node number to keep consistent order
        std::sort(favoritedNodes.begin(), favoritedNodes.end(), [](meshtastic_NodeInfoLite *a, meshtastic_NodeInfoLite *b) {
            return a->num < b->num;
        });
    }

    if (favoritedNodes.empty()) return;

    int nodeIndex = state->currentFrame - (screen->frameCount - favoritedNodes.size());
    if (nodeIndex < 0 || nodeIndex >= (int)favoritedNodes.size()) return;

    meshtastic_NodeInfoLite *node = favoritedNodes[nodeIndex];
    if (!node || node->num == nodeDB->getNodeNum() || !node->is_favorite) return;

    display->clear();

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Title: Short Name centered in header row ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const int centerX = x + SCREEN_WIDTH / 2;
    const char *shortName = (node->has_user && haveGlyphs(node->user.short_name)) ? node->user.short_name : "Node";

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
        display->setColor(BLACK);

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(centerX, textY, shortName);
    if (config.display.heading_bold)
        display->drawString(centerX + 1, textY, shortName);

    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    const char *username = node->has_user ? node->user.long_name : "Unknown Name";

    static char signalStr[20];
    if (node->hops_away > 0)
        snprintf(signalStr, sizeof(signalStr), "Hops: %d", node->hops_away);
    else
        snprintf(signalStr, sizeof(signalStr), "Signal: %d%%", clamp((int)((node->snr + 10) * 5), 0, 100));

    static char seenStr[20];
    uint32_t seconds = sinceLastSeen(node);
    if (seconds == 0 || seconds == UINT32_MAX) {
        snprintf(seenStr, sizeof(seenStr), "Heard: ?");
    } else {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        snprintf(seenStr, sizeof(seenStr), (days > 365 ? "Heard: ?" : "Heard: %d%c ago"),
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }

    static char distStr[20];
    strncpy(distStr,
            (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) ? "? mi ?°" : "? km ?°",
            sizeof(distStr));

    // === First Row: Long Name ===
    display->drawString(x, compactFirstLine, username);

    // === Second Row: Last Seen ===
    display->drawString(x, compactSecondLine, seenStr);

    // === Third Row: Signal Strength or Hops ===
    display->drawString(x, compactThirdLine, signalStr);

    // === Fourth Row: Distance/Bearing ===
    display->drawString(x, compactFourthLine, distStr);

    // === Compass Rendering (resized like CompassAndLocation screen) ===
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const int16_t topY = compactFirstLine;
    const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1);
    const int16_t usableHeight = bottomY - topY - 5;

    int16_t compassRadius = usableHeight / 2;
    if (compassRadius < 8)
        compassRadius = 8;
    const int16_t compassDiam = compassRadius * 2;
    const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;
    const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

    bool hasNodeHeading = false;

    if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading())) {
        const auto &op = ourNode->position;
        float myHeading = screen->hasHeading() ? screen->getHeading() * PI / 180
                                               : screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        screen->drawCompassNorth(display, compassX, compassY, myHeading);

        if (nodeDB->hasValidPosition(node)) {
            hasNodeHeading = true;
            const auto &p = node->position;
            float d = GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i),
                                                DegD(op.latitude_i), DegD(op.longitude_i));
            float bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i),
                                              DegD(p.latitude_i), DegD(p.longitude_i));
            if (!config.display.compass_north_top) bearing -= myHeading;

            screen->drawNodeHeading(display, compassX, compassY, compassDiam, bearing);

            float bearingDeg = fmodf((bearing < 0 ? bearing + 2 * PI : bearing) * 180 / PI, 360.0f);
            if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
                snprintf(distStr, sizeof(distStr), d < 2 * MILES_TO_FEET ? "%.0fft   %.0f°" : "%.1fmi   %.0f°",
                         d * METERS_TO_FEET / (d < 2 * MILES_TO_FEET ? 1 : MILES_TO_FEET), bearingDeg);
            else
                snprintf(distStr, sizeof(distStr), d < 2000 ? "%.0fm   %.0f°" : "%.1fkm   %.0f°",
                         d / (d < 2000 ? 1 : 1000), bearingDeg);
        }
    }

    if (!hasNodeHeading)
        display->drawString(compassX - FONT_HEIGHT_SMALL / 4, compassY - FONT_HEIGHT_SMALL / 2, "?");

    display->drawCircle(compassX, compassY, compassRadius);
}

// Combined dynamic node list frame cycling through LastHeard, HopSignal, and Distance modes
// Uses a single frame and changes data every few seconds (E-Ink variant is separate)

// =============================
// Shared Types and Structures
// =============================
typedef void (*EntryRenderer)(OLEDDisplay *, meshtastic_NodeInfoLite *, int16_t, int16_t, int);
typedef void (*NodeExtrasRenderer)(OLEDDisplay *, meshtastic_NodeInfoLite *, int16_t, int16_t, int, float, double, double);

struct NodeEntry {
    meshtastic_NodeInfoLite *node;
    uint32_t lastHeard;
    float cachedDistance = -1.0f; // Only used in distance mode
};

// =============================
// Shared Enums and Timing Logic
// =============================
enum NodeListMode { MODE_LAST_HEARD = 0, MODE_HOP_SIGNAL = 1, MODE_DISTANCE = 2, MODE_COUNT = 3 };

static NodeListMode currentMode = MODE_LAST_HEARD;
static int scrollIndex = 0;

// Use dynamic timing based on mode
unsigned long getModeCycleIntervalMs()
{
    // return (currentMode == MODE_DISTANCE) ? 3000 : 2000;
    return 3000;
}

// h! Calculates bearing between two lat/lon points (used for compass)
float calculateBearing(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    lat1 = lat1 * DEG_TO_RAD;
    lat2 = lat2 * DEG_TO_RAD;

    double y = sin(dLon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
    double initialBearing = atan2(y, x);

    return fmod((initialBearing * RAD_TO_DEG + 360), 360); // Normalize to 0-360°
}

int calculateMaxScroll(int totalEntries, int visibleRows)
{
    int totalRows = (totalEntries + 1) / 2;
    return std::max(0, totalRows - visibleRows);
}

// =============================
// Node Sorting and Scroll Helpers
// =============================
String getSafeNodeName(meshtastic_NodeInfoLite *node)
{
    String nodeName = "?";
    if (node->has_user && strlen(node->user.short_name) > 0) {
        bool valid = true;
        const char *name = node->user.short_name;
        for (size_t i = 0; i < strlen(name); i++) {
            uint8_t c = (uint8_t)name[i];
            if (c < 32 || c > 126) {
                valid = false;
                break;
            }
        }
        if (valid) {
            nodeName = name;
        } else {
            char idStr[6];
            snprintf(idStr, sizeof(idStr), "%04X", (uint16_t)(node->num & 0xFFFF));
            nodeName = String(idStr);
        }
    }
    if (node->is_favorite)
        nodeName = "*" + nodeName;
    return nodeName;
}

void retrieveAndSortNodes(std::vector<NodeEntry> &nodeList)
{
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    bool hasValidSelf = nodeDB->hasValidPosition(ourNode);

    size_t numNodes = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < numNodes; i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum())
            continue;

        NodeEntry entry;
        entry.node = node;
        entry.lastHeard = sinceLastSeen(node);
        entry.cachedDistance = -1.0f;

        // Pre-calculate distance if we're about to render distance screen
        if (currentMode == MODE_DISTANCE && hasValidSelf && nodeDB->hasValidPosition(node)) {
            float lat1 = ourNode->position.latitude_i * 1e-7f;
            float lon1 = ourNode->position.longitude_i * 1e-7f;
            float lat2 = node->position.latitude_i * 1e-7f;
            float lon2 = node->position.longitude_i * 1e-7f;

            float dLat = (lat2 - lat1) * DEG_TO_RAD;
            float dLon = (lon2 - lon1) * DEG_TO_RAD;
            float a =
                sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
            float c = 2 * atan2(sqrt(a), sqrt(1 - a));
            entry.cachedDistance = 6371.0f * c; // Earth radius in km
        }

        nodeList.push_back(entry);
    }

    std::sort(nodeList.begin(), nodeList.end(), [](const NodeEntry &a, const NodeEntry &b) {
        bool aFav = a.node->is_favorite;
        bool bFav = b.node->is_favorite;
        if (aFav != bFav)
            return aFav > bFav;
        if (a.lastHeard == 0 || a.lastHeard == UINT32_MAX)
            return false;
        if (b.lastHeard == 0 || b.lastHeard == UINT32_MAX)
            return true;
        return a.lastHeard < b.lastHeard;
    });
}

void drawColumnSeparator(OLEDDisplay *display, int16_t x, int16_t yStart, int16_t yEnd)
{
    int columnWidth = display->getWidth() / 2;
    int separatorX = x + columnWidth - 2;
    display->drawLine(separatorX, yStart, separatorX, yEnd);
}

void drawScrollbar(OLEDDisplay *display, int visibleNodeRows, int totalEntries, int scrollIndex, int columns, int scrollStartY)
{
    const int rowHeight = FONT_HEIGHT_SMALL - 3;
    const int totalVisualRows = (totalEntries + columns - 1) / columns;
    if (totalVisualRows <= visibleNodeRows)
        return;
    const int scrollAreaHeight = visibleNodeRows * rowHeight;
    const int scrollbarX = display->getWidth() - 6;
    const int scrollbarWidth = 4;
    const int scrollBarHeight = (scrollAreaHeight * visibleNodeRows) / totalVisualRows;
    const int scrollBarY = scrollStartY + (scrollAreaHeight * scrollIndex) / totalVisualRows;
    display->drawRect(scrollbarX, scrollStartY, scrollbarWidth, scrollAreaHeight);
    display->fillRect(scrollbarX, scrollBarY, scrollbarWidth, scrollBarHeight);
}

// =============================
// Shared Node List Screen Logic
// =============================
void drawNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                        EntryRenderer renderer, NodeExtrasRenderer extras = nullptr, float heading = 0, double lat = 0,
                        double lon = 0)
{
    const int COMMON_HEADER_HEIGHT = FONT_HEIGHT_SMALL - 1;
    const int rowYOffset = FONT_HEIGHT_SMALL - 3;

    int columnWidth = display->getWidth() / 2;

    display->clear();

    // === Draw the battery/time header ===
    graphics::drawCommonHeader(display, x, y);

    // === Manually draw the centered title within the header ===
    const int highlightHeight = COMMON_HEADER_HEIGHT;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const int centerX = x + SCREEN_WIDTH / 2;

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
        display->setColor(BLACK);

    display->drawString(centerX, textY, title);
    if (config.display.heading_bold)
        display->drawString(centerX + 1, textY, title);

    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Space below header ===
    y += COMMON_HEADER_HEIGHT;

    // === Fetch and display sorted node list ===
    std::vector<NodeEntry> nodeList;
    retrieveAndSortNodes(nodeList);

    int totalEntries = nodeList.size();
    int totalRowsAvailable = (display->getHeight() - y) / rowYOffset;
    #ifdef USE_EINK
        totalRowsAvailable -= 1;
    #endif
    int visibleNodeRows = totalRowsAvailable;
    int totalColumns = 2;

    int startIndex = scrollIndex * visibleNodeRows * totalColumns;
    int endIndex = std::min(startIndex + visibleNodeRows * totalColumns, totalEntries);

    int yOffset = 0;
    int col = 0;
    int lastNodeY = y;
    int shownCount = 0;
    int rowCount = 0;

    for (int i = startIndex; i < endIndex; ++i) {
        int xPos = x + (col * columnWidth);
        int yPos = y + yOffset;
        renderer(display, nodeList[i].node, xPos, yPos, columnWidth);

        // ✅ Actually render the compass arrow
        if (extras) {
            extras(display, nodeList[i].node, xPos, yPos, columnWidth, heading, lat, lon);
        }

        lastNodeY = std::max(lastNodeY, yPos + FONT_HEIGHT_SMALL);
        yOffset += rowYOffset;
        shownCount++;
        rowCount++;

        if (rowCount >= totalRowsAvailable) {
            yOffset = 0;
            rowCount = 0;
            col++;
            if (col > (totalColumns - 1))
                break;
        }
    }

    // === Draw column separator
    if (shownCount > 0) {
        const int firstNodeY = y + 3;
        drawColumnSeparator(display, x, firstNodeY, lastNodeY);
    }

    const int scrollStartY = y + 3;
    drawScrollbar(display, visibleNodeRows, totalEntries, scrollIndex, 2, scrollStartY);
}

// =============================
// Shared Dynamic Entry Renderers
// =============================
void drawEntryLastHeard(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int timeOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 7 : 10) // Offset for Wide Screens (Left Column:Right Column)
                                          : (isLeftCol ? 3 : 7); // Offset for Narrow Screens (Left Column:Right Column)

    String nodeName = getSafeNodeName(node);

    char timeStr[10];
    uint32_t seconds = sinceLastSeen(node);
    if (seconds == 0 || seconds == UINT32_MAX) {
        snprintf(timeStr, sizeof(timeStr), "?");
    } else {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        snprintf(timeStr, sizeof(timeStr), (days > 365 ? "?" : "%d%c"),
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(x, y, nodeName);

    int rightEdge = x + columnWidth - timeOffset;
    int textWidth = display->getStringWidth(timeStr);
    display->drawString(rightEdge - textWidth, y, timeStr);
}

// ****************************
// *   Hops / Signal Screen   *
// ****************************
void drawEntryHopSignal(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);

    int nameMaxWidth = columnWidth - 25;
    int barsOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 16 : 20)  // Offset for Wide Screens (Left Column:Right Column)
                                          : (isLeftCol ? 15 : 19); // Offset for Narrow Screens (Left Column:Right Column)
    int hopOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 22 : 28)   // Offset for Wide Screens (Left Column:Right Column)
                                         : (isLeftCol ? 18 : 20);  // Offset for Narrow Screens (Left Column:Right Column)
    int barsXOffset = columnWidth - barsOffset;

    String nodeName = getSafeNodeName(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x, y, nameMaxWidth, nodeName);

    char hopStr[6] = "";
    if (node->has_hops_away && node->hops_away > 0)
        snprintf(hopStr, sizeof(hopStr), "[%d]", node->hops_away);

    if (hopStr[0] != '\0') {
        int rightEdge = x + columnWidth - hopOffset;
        int textWidth = display->getStringWidth(hopStr);
        display->drawString(rightEdge - textWidth, y, hopStr);
    }

    int bars = (node->snr > 5) ? 4 : (node->snr > 0) ? 3 : (node->snr > -5) ? 2 : (node->snr > -10) ? 1 : 0;
    int barWidth = 2;
    int barStartX = x + barsXOffset;
    int barStartY = y + (FONT_HEIGHT_SMALL / 2) + 2;

    for (int b = 0; b < 4; b++) {
        if (b < bars) {
            int height = (b * 2);
            display->fillRect(barStartX + (b * (barWidth + 1)), barStartY - height, barWidth, height);
        }
    }
}

// **************************
// *     Distance Screen    *
// **************************
void drawNodeDistance(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int nameMaxWidth = columnWidth - (SCREEN_WIDTH > 128 ? (isLeftCol ? 25 : 28) : (isLeftCol ? 20 : 22));

    String nodeName = getSafeNodeName(node);
    char distStr[10] = "";

    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (nodeDB->hasValidPosition(ourNode) && nodeDB->hasValidPosition(node)) {
        double lat1 = ourNode->position.latitude_i * 1e-7;
        double lon1 = ourNode->position.longitude_i * 1e-7;
        double lat2 = node->position.latitude_i * 1e-7;
        double lon2 = node->position.longitude_i * 1e-7;

        double earthRadiusKm = 6371.0;
        double dLat = (lat2 - lat1) * DEG_TO_RAD;
        double dLon = (lon2 - lon1) * DEG_TO_RAD;

        double a =
            sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        double distanceKm = earthRadiusKm * c;

        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            double miles = distanceKm * 0.621371;
            if (miles < 0.1) {
                int feet = (int)(miles * 5280);
                if (feet < 1000)
                    snprintf(distStr, sizeof(distStr), "%dft", feet);
                else
                    snprintf(distStr, sizeof(distStr), "¼mi"); // 4-char max
            } else {
                int roundedMiles = (int)(miles + 0.5);
                if (roundedMiles < 1000)
                    snprintf(distStr, sizeof(distStr), "%dmi", roundedMiles);
                else
                    snprintf(distStr, sizeof(distStr), "999"); // Max display cap
            }
        } else {
            if (distanceKm < 1.0) {
                int meters = (int)(distanceKm * 1000);
                if (meters < 1000)
                    snprintf(distStr, sizeof(distStr), "%dm", meters);
                else
                    snprintf(distStr, sizeof(distStr), "1k");
            } else {
                int km = (int)(distanceKm + 0.5);
                if (km < 1000)
                    snprintf(distStr, sizeof(distStr), "%dk", km);
                else
                    snprintf(distStr, sizeof(distStr), "999");
            }
        }
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x, y, nameMaxWidth, nodeName);

    if (strlen(distStr) > 0) {
        int offset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 7 : 10) // Offset for Wide Screens (Left Column:Right Column)
                                          : (isLeftCol ? 5 : 8); // Offset for Narrow Screens (Left Column:Right Column)
        int rightEdge = x + columnWidth - offset;
        int textWidth = display->getStringWidth(distStr);
        display->drawString(rightEdge - textWidth, y, distStr);
    }
}

// =============================
// Dynamic Unified Entry Renderer
// =============================
void drawEntryDynamic(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    switch (currentMode) {
    case MODE_LAST_HEARD:
        drawEntryLastHeard(display, node, x, y, columnWidth);
        break;
    case MODE_HOP_SIGNAL:
        drawEntryHopSignal(display, node, x, y, columnWidth);
        break;
    case MODE_DISTANCE:
        drawNodeDistance(display, node, x, y, columnWidth);
        break;
    default:
        break; // Silences warning for MODE_COUNT or unexpected values
    }
}

const char *getCurrentModeTitle(int screenWidth)
{
    switch (currentMode) {
    case MODE_LAST_HEARD:
        return "Node List";
    case MODE_HOP_SIGNAL:
        return (screenWidth > 128) ? "Hops|Signals" : "Hop|Sig";
    case MODE_DISTANCE:
        return "Distances";
    default:
        return "Nodes";
    }
}

// =============================
// OLED/TFT Version (cycles every few seconds)
// =============================
#ifndef USE_EINK
static void drawDynamicNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Static variables to track mode and duration
    static NodeListMode lastRenderedMode = MODE_COUNT;
    static unsigned long modeStartTime = 0;

    unsigned long now = millis();

    // On very first call (on boot or state enter)
    if (lastRenderedMode == MODE_COUNT) {
        currentMode = MODE_LAST_HEARD;
        modeStartTime = now;
    }

    // Time to switch to next mode?
    if (now - modeStartTime >= getModeCycleIntervalMs()) {
        currentMode = static_cast<NodeListMode>((currentMode + 1) % MODE_COUNT);
        modeStartTime = now;
    }

    // Render screen based on currentMode
    const char *title = getCurrentModeTitle(display->getWidth());
    drawNodeListScreen(display, state, x, y, title, drawEntryDynamic);

    // Track the last mode to avoid reinitializing modeStartTime
    lastRenderedMode = currentMode;
}
#endif

// =============================
// E-Ink Version (mode set once per boot)
// =============================
#ifdef USE_EINK
static void drawDynamicNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->ticksSinceLastStateSwitch == 0) {
        currentMode = MODE_LAST_HEARD;
    }
    const char *title = getCurrentModeTitle(display->getWidth());
    drawNodeListScreen(display, state, x, y, title, drawEntryDynamic);
}
#endif

// Add these below (still inside #ifdef USE_EINK if you prefer):
#ifdef USE_EINK
static void drawLastHeardScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = "Node List";
    drawNodeListScreen(display, state, x, y, title, drawEntryLastHeard);
}

static void drawHopSignalScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = (display->getWidth() > 128) ? "Hops|Signals" : "Hop|Sig";
    drawNodeListScreen(display, state, x, y, title, drawEntryHopSignal);
}

static void drawDistanceScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = "Distances";
    drawNodeListScreen(display, state, x, y, title, drawNodeDistance);
}
#endif
// Helper function: Draw a single node entry for Node List (Modified for Compass Screen)
void drawEntryCompass(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);

    // Adjust max text width depending on column and screen width
    int nameMaxWidth = columnWidth - (SCREEN_WIDTH > 128 ? (isLeftCol ? 25 : 28) : (isLeftCol ? 20 : 22));

    String nodeName = getSafeNodeName(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x, y, nameMaxWidth, nodeName);
}
void drawCompassArrow(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth, float myHeading,
                      double userLat, double userLon)
{
    if (!nodeDB->hasValidPosition(node))
        return;

    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int arrowXOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 22 : 24) : (isLeftCol ? 12 : 18);

    int centerX = x + columnWidth - arrowXOffset;
    int centerY = y + FONT_HEIGHT_SMALL / 2;

    double nodeLat = node->position.latitude_i * 1e-7;
    double nodeLon = node->position.longitude_i * 1e-7;
    float bearingToNode = calculateBearing(userLat, userLon, nodeLat, nodeLon);
    float relativeBearing = fmod((bearingToNode - myHeading + 360), 360);
    float angle = relativeBearing * DEG_TO_RAD;

    // Shrink size by 2px
    int size = FONT_HEIGHT_SMALL - 5;
    float halfSize = size / 2.0;

    // Point of the arrow
    int tipX = centerX + halfSize * cos(angle);
    int tipY = centerY - halfSize * sin(angle);

    float baseAngle = radians(35);
    float sideLen = halfSize * 0.95;
    float notchInset = halfSize * 0.35;

    // Left and right corners
    int leftX = centerX + sideLen * cos(angle + PI - baseAngle);
    int leftY = centerY - sideLen * sin(angle + PI - baseAngle);

    int rightX = centerX + sideLen * cos(angle + PI + baseAngle);
    int rightY = centerY - sideLen * sin(angle + PI + baseAngle);

    // Center notch (cut-in)
    int notchX = centerX - notchInset * cos(angle);
    int notchY = centerY + notchInset * sin(angle);

    // Draw the chevron-style arrowhead
    display->fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY);
    display->fillTriangle(tipX, tipY, notchX, notchY, rightX, rightY);
}

// Public screen entry for compass
static void drawNodeListWithCompasses(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    float heading = 0;
    bool validHeading = false;
    double lat = 0;
    double lon = 0;

#if HAS_GPS
    geoCoord.updateCoords(int32_t(gpsStatus->getLatitude()), int32_t(gpsStatus->getLongitude()),
                          int32_t(gpsStatus->getAltitude()));
    lat = geoCoord.getLatitude() * 1e-7;
    lon = geoCoord.getLongitude() * 1e-7;

    if (screen->hasHeading()) {
        heading = screen->getHeading(); // degrees
        validHeading = true;
    } else {
        heading = screen->estimatedHeading(lat, lon);
        validHeading = !isnan(heading);
    }
#endif

    if (!validHeading)
        return;

    drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassArrow, heading, lat, lon);
}

// ****************************
// * Device Focused Screen    *
// ****************************
static void drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Content below header ===

    // === First Row: Region / Channel Utilization and Uptime ===
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    // Display Region and Channel Utilization
    drawNodes(display, x + 1, compactFirstLine + 2, nodeStatus, -1, false, "online");

    uint32_t uptime = millis() / 1000;
    char uptimeStr[6];
    uint32_t minutes = uptime / 60, hours = minutes / 60, days = hours / 24;

    if (days > 365) {
        snprintf(uptimeStr, sizeof(uptimeStr), "?");
    } else {
        snprintf(uptimeStr, sizeof(uptimeStr), "%d%c",
                 days      ? days
                 : hours   ? hours
                 : minutes ? minutes
                           : (int)uptime,
                 days      ? 'd'
                 : hours   ? 'h'
                 : minutes ? 'm'
                           : 's');
    }

    char uptimeFullStr[16];
    snprintf(uptimeFullStr, sizeof(uptimeFullStr), "Uptime: %s", uptimeStr);
    display->drawString(SCREEN_WIDTH - display->getStringWidth(uptimeFullStr), compactFirstLine, uptimeFullStr);

    config.display.heading_bold = origBold;

    // === Second Row: Satellites and Voltage ===
    config.display.heading_bold = false;

#if HAS_GPS
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        String displayLine = "";
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        display->drawString(0, compactSecondLine, displayLine);
    } else {
        drawGPS(display, 0, compactSecondLine + 3, gpsStatus);
    }
#endif

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;
        snprintf(batStr, sizeof(batStr), "%01d.%02dV", batV, batCv);
        display->drawString(x + SCREEN_WIDTH - display->getStringWidth(batStr), compactSecondLine, batStr);
    } else {
        display->drawString(x + SCREEN_WIDTH - display->getStringWidth("USB"), compactSecondLine, String("USB"));
    }

    config.display.heading_bold = origBold;

    // === Third & Fourth Rows: Node Identity ===
    int textWidth = 0;
    int nameX = 0;
    int yOffset = (SCREEN_WIDTH > 128) ? 0 : 7;
    const char *longName = nullptr;
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (ourNode && ourNode->has_user && strlen(ourNode->user.long_name) > 0) {
        longName = ourNode->user.long_name;
    }
    uint8_t dmac[6];
    char shortnameble[35];
    getMacAddr(dmac);
    snprintf(ourId, sizeof(ourId), "%02x%02x", dmac[4], dmac[5]);
    snprintf(shortnameble, sizeof(shortnameble), "%s", haveGlyphs(owner.short_name) ? owner.short_name : "");

    char combinedName[50];
    snprintf(combinedName, sizeof(combinedName), "%s (%s)", longName, shortnameble);
    if(SCREEN_WIDTH - (display->getStringWidth(longName) + display->getStringWidth(shortnameble)) > 10){
        // === Third Row: combinedName Centered ===
        size_t len = strlen(combinedName);
        if (len >= 3 && strcmp(combinedName + len - 3, " ()") == 0) {
            combinedName[len - 3] = '\0';  // Remove the last three characters
        }
        textWidth = display->getStringWidth(combinedName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, compactThirdLine + yOffset, combinedName);
    } else {
        // === Third Row: LongName Centered ===
        textWidth = display->getStringWidth(longName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        yOffset = (strcmp(shortnameble, "") == 0) ? 1 : 0;
        if(yOffset == 1){
            yOffset = (SCREEN_WIDTH > 128) ? 0 : 7;
        }
        display->drawString(nameX, compactThirdLine + yOffset, longName);

        // === Fourth Row: ShortName Centered ===
        textWidth = display->getStringWidth(shortnameble);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, compactFourthLine, shortnameble);
    }

    // === Fifth Row: Bluetooth Off Icon ===
    if (!config.bluetooth.enabled) {
        const int iconX = 0; // Left aligned
        const int iconY = compactFifthLine + ((SCREEN_WIDTH > 128) ? 42 : 2);
        display->drawXbm(iconX, iconY, bluetoothdisabled_width, bluetoothdisabled_height, bluetoothdisabled);
        display->drawLine(iconX, iconY, iconX + 9, iconY + 5);
    }
}

// ****************************
// * LoRa Focused Screen      *
// ****************************
static void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Draw title (aligned with header baseline) ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const char *titleStr = (SCREEN_WIDTH > 128) ? "LoRa Info" : "LoRa";
    const int centerX = x + SCREEN_WIDTH / 2;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(centerX, textY, titleStr);
    if (config.display.heading_bold) {
        display->drawString(centerX + 1, textY, titleStr);
    }
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === First Row: Region / BLE Name ===
    drawNodes(display, x, compactFirstLine + 3, nodeStatus, 0, true);

    uint8_t dmac[6];
    char shortnameble[35];
    getMacAddr(dmac);
    snprintf(ourId, sizeof(ourId), "%02x%02x", dmac[4], dmac[5]);
    snprintf(shortnameble, sizeof(shortnameble), "BLE: %s", ourId);
    int textWidth = display->getStringWidth(shortnameble);
    int nameX = (SCREEN_WIDTH - textWidth);
    display->drawString(nameX, compactFirstLine, shortnameble);

    // === Second Row: Radio Preset ===
    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false);
    char regionradiopreset[25];
    const char *region = myRegion ? myRegion->name : NULL;
    snprintf(regionradiopreset, sizeof(regionradiopreset), "%s/%s", region, mode);
    textWidth = display->getStringWidth(regionradiopreset);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, compactSecondLine, regionradiopreset);

    // === Third Row: Frequency / ChanNum ===
    char frequencyslot[35];
    char freqStr[16];
    float freq = RadioLibInterface::instance->getFreq();
    snprintf(freqStr, sizeof(freqStr), "%.3f", freq);
    if(config.lora.channel_num == 0){
        snprintf(frequencyslot, sizeof(frequencyslot), "Freq: %s", freqStr);
    } else {
        snprintf(frequencyslot, sizeof(frequencyslot), "Freq/Chan: %s (%d)", freqStr, config.lora.channel_num);
    }
    size_t len = strlen(frequencyslot);
    if (len >= 4 && strcmp(frequencyslot + len - 4, " (0)") == 0) {
        frequencyslot[len - 4] = '\0';  // Remove the last three characters
    }
    textWidth = display->getStringWidth(frequencyslot);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    display->drawString(nameX, compactThirdLine, frequencyslot);

    // === Fourth Row: Channel Utilization ===
    const char *chUtil = "ChUtil:";
    char chUtilPercentage[10];
    snprintf(chUtilPercentage, sizeof(chUtilPercentage), "%2.0f%%", airTime->channelUtilizationPercent());

    int chUtil_x = (SCREEN_WIDTH > 128) ? display->getStringWidth(chUtil) + 10 : display->getStringWidth(chUtil) + 5;
    int chUtil_y = compactFourthLine + 3;

    int chutil_bar_width = (SCREEN_WIDTH > 128) ? 100 : 50;
    int chutil_bar_height = (SCREEN_WIDTH > 128) ? 12 : 7;
    int extraoffset = (SCREEN_WIDTH > 128) ? 6 : 3;
    int chutil_percent = airTime->channelUtilizationPercent();

    int centerofscreen = SCREEN_WIDTH / 2;
    int total_line_content_width = (chUtil_x + chutil_bar_width + display->getStringWidth(chUtilPercentage) + extraoffset) / 2;
    int starting_position = centerofscreen - total_line_content_width;

    display->drawString(starting_position, compactFourthLine, chUtil);

    // Force 56% or higher to show a full 100% bar, text would still show related percent.
    if (chutil_percent >= 61) {
        chutil_percent = 100;
    }

    // Weighting for nonlinear segments
    float milestone1 = 25;
    float milestone2 = 40;
    float weight1 = 0.45; // Weight for 0–25%
    float weight2 = 0.35; // Weight for 25–40%
    float weight3 = 0.20; // Weight for 40–100%
    float totalWeight = weight1 + weight2 + weight3;

    int seg1 = chutil_bar_width * (weight1 / totalWeight);
    int seg2 = chutil_bar_width * (weight2 / totalWeight);
    int seg3 = chutil_bar_width * (weight3 / totalWeight);

    int fillRight = 0;

    if (chutil_percent <= milestone1) {
        fillRight = (seg1 * (chutil_percent / milestone1));
    } else if (chutil_percent <= milestone2) {
        fillRight = seg1 + (seg2 * ((chutil_percent - milestone1) / (milestone2 - milestone1)));
    } else {
        fillRight = seg1 + seg2 + (seg3 * ((chutil_percent - milestone2) / (100 - milestone2)));
    }

    // Draw outline
    display->drawRect(starting_position + chUtil_x, chUtil_y, chutil_bar_width, chutil_bar_height);

    // Fill progress
    if (fillRight > 0) {
        display->fillRect(starting_position + chUtil_x, chUtil_y, fillRight, chutil_bar_height);
    }

    display->drawString(starting_position + chUtil_x + chutil_bar_width + extraoffset, compactFourthLine, chUtilPercentage);
}

// ****************************
// * My Position Screen       *
// ****************************
static void drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Draw title ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const char *titleStr = "GPS";
    const int centerX = x + SCREEN_WIDTH / 2;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(centerX, textY, titleStr);
    if (config.display.heading_bold) {
        display->drawString(centerX + 1, textY, titleStr);
    }
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === First Row: My Location ===
#if HAS_GPS
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    String Satelite_String = "Sat:";
    display->drawString(0, compactFirstLine, Satelite_String);
    String displayLine = "";
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        display->drawString(display->getStringWidth(Satelite_String) + 3, compactFirstLine, displayLine);
    } else {
        drawGPS(display, display->getStringWidth(Satelite_String) + 3, compactFirstLine + 3, gpsStatus);
    }

    config.display.heading_bold = origBold;

    // === Update GeoCoord ===
    geoCoord.updateCoords(int32_t(gpsStatus->getLatitude()), int32_t(gpsStatus->getLongitude()),
                          int32_t(gpsStatus->getAltitude()));

    // === Determine Compass Heading ===
    float heading;
    bool validHeading = false;

    if (screen->hasHeading()) {
        heading = radians(screen->getHeading());
        validHeading = true;
    } else {
        heading = screen->estimatedHeading(geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
        validHeading = !isnan(heading);
    }

    // If GPS is off, no need to display these parts
    if (displayLine != "GPS off" && displayLine != "No GPS") {

        // === Second Row: Altitude ===
        String displayLine;
        displayLine = "Alt: " + String(geoCoord.getAltitude()) + "m";
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            displayLine = "Alt: " + String(geoCoord.getAltitude() * METERS_TO_FEET) + "ft";
        display->drawString(x, compactSecondLine, displayLine);

        // === Third Row: Latitude ===
        char latStr[32];
        snprintf(latStr, sizeof(latStr), "Lat: %.5f", geoCoord.getLatitude() * 1e-7);
        display->drawString(x, compactThirdLine, latStr);

        // === Fourth Row: Longitude ===
        char lonStr[32];
        snprintf(lonStr, sizeof(lonStr), "Lon: %.5f", geoCoord.getLongitude() * 1e-7);
        display->drawString(x, compactFourthLine, lonStr);

        if(SCREEN_HEIGHT > 64){
            // === Fifth Row: Date ===
            uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
            char datetimeStr[25];
            bool showTime = false;  // set to true for full datetime
            formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display, showTime);
            char fullLine[40];
            snprintf(fullLine, sizeof(fullLine), "Date: %s", datetimeStr);
            display->drawString(0, compactFifthLine, fullLine);
        }
    }

    // === Draw Compass if heading is valid ===
    if (validHeading) {
        const int16_t topY = compactFirstLine;
        const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1); // nav row height
        const int16_t usableHeight = bottomY - topY - 5;

        int16_t compassRadius = usableHeight / 2;
        if (compassRadius < 8)
            compassRadius = 8;
        const int16_t compassDiam = compassRadius * 2;
        const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;

        // Center vertically and nudge down slightly to keep "N" clear of header
        const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

        // Draw compass
        screen->drawNodeHeading(display, compassX, compassY, compassDiam, -heading);
        display->drawCircle(compassX, compassY, compassRadius);

        // "N" label
        float northAngle = -heading;
        float radius = compassRadius;
        int16_t nX = compassX + (radius - 1) * sin(northAngle);
        int16_t nY = compassY - (radius - 1) * cos(northAngle);
        int16_t nLabelWidth = display->getStringWidth("N") + 2;
        int16_t nLabelHeightBox = FONT_HEIGHT_SMALL + 1;

        display->setColor(BLACK);
        display->fillRect(nX - nLabelWidth / 2, nY - nLabelHeightBox / 2, nLabelWidth, nLabelHeightBox);
        display->setColor(WHITE);
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(nX, nY - FONT_HEIGHT_SMALL / 2, "N");
    }
#endif
}

// ****************************
// *      Memory Screen       *
// ****************************
static void drawMemoryScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Draw title ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const char *titleStr = (SCREEN_WIDTH > 128) ? "Memory" : "Mem";
    const int centerX = x + SCREEN_WIDTH / 2;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(centerX, textY, titleStr);
    if (config.display.heading_bold) {
        display->drawString(centerX + 1, textY, titleStr);
    }
    display->setColor(WHITE);

    // === Layout ===
    int contentY = y + FONT_HEIGHT_SMALL;
    const int rowYOffset = FONT_HEIGHT_SMALL - 3;
    const int barHeight = 6;
    const int labelX = x;
    const int barsOffset = (SCREEN_WIDTH > 128) ? 24 : 0;
    const int barX = x + 40 + barsOffset;

    int rowY = contentY;

    // === Heap delta tracking (disabled) ===
    /*
    static uint32_t previousHeapFree = 0;
    static int32_t totalHeapDelta = 0;
    static int deltaChangeCount = 0;
    */

    auto drawUsageRow = [&](const char *label, uint32_t used, uint32_t total, bool isHeap = false) {
        if (total == 0)
            return;

        int percent = (used * 100) / total;

        char combinedStr[24];
        if (SCREEN_WIDTH > 128) {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%  %lu/%luKB", (percent > 80) ? "! " : "", percent, used / 1024,
                     total / 1024);
        } else {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%", (percent > 80) ? "! " : "", percent);
        }

        int textWidth = display->getStringWidth(combinedStr);
        int adjustedBarWidth = SCREEN_WIDTH - barX - textWidth - 6;
        if (adjustedBarWidth < 10)
            adjustedBarWidth = 10;

        int fillWidth = (used * adjustedBarWidth) / total;

        // Label
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(labelX, rowY, label);

        // Bar
        int barY = rowY + (FONT_HEIGHT_SMALL - barHeight) / 2;
        display->setColor(WHITE);
        display->drawRect(barX, barY, adjustedBarWidth, barHeight);

        display->fillRect(barX, barY, fillWidth, barHeight);
        display->setColor(WHITE);

        // Value string
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(SCREEN_WIDTH - 2, rowY, combinedStr);

        rowY += rowYOffset;

        // === Heap delta display (disabled) ===
        /*
        if (isHeap && previousHeapFree > 0) {
            int32_t delta = (int32_t)(memGet.getFreeHeap() - previousHeapFree);
            if (delta != 0) {
                totalHeapDelta += delta;
                deltaChangeCount++;

                char deltaStr[16];
                snprintf(deltaStr, sizeof(deltaStr), "%ld", delta);

                int deltaX = centerX - display->getStringWidth(deltaStr) / 2 - 8;
                int deltaY = rowY + 1;

                // Triangle
                if (delta > 0) {
                    display->drawLine(deltaX, deltaY + 6, deltaX + 3, deltaY);
                    display->drawLine(deltaX + 3, deltaY, deltaX + 6, deltaY + 6);
                    display->drawLine(deltaX, deltaY + 6, deltaX + 6, deltaY + 6);
                } else {
                    display->drawLine(deltaX, deltaY, deltaX + 3, deltaY + 6);
                    display->drawLine(deltaX + 3, deltaY + 6, deltaX + 6, deltaY);
                    display->drawLine(deltaX, deltaY, deltaX + 6, deltaY);
                }

                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(centerX + 6, deltaY, deltaStr);
                rowY += rowYOffset;
            }
        }

        if (isHeap) {
            previousHeapFree = memGet.getFreeHeap();
        }
        */
    };

    // === Memory values ===
    uint32_t heapUsed = memGet.getHeapSize() - memGet.getFreeHeap();
    uint32_t heapTotal = memGet.getHeapSize();

    uint32_t psramUsed = memGet.getPsramSize() - memGet.getFreePsram();
    uint32_t psramTotal = memGet.getPsramSize();

    uint32_t flashUsed = 0, flashTotal = 0;
#ifdef ESP32
    flashUsed = FSCom.usedBytes();
    flashTotal = FSCom.totalBytes();
#endif

    uint32_t sdUsed = 0, sdTotal = 0;
    bool hasSD = false;
    /*
    #ifdef HAS_SDCARD
        hasSD = SD.cardType() != CARD_NONE;
        if (hasSD) {
            sdUsed = SD.usedBytes();
            sdTotal = SD.totalBytes();
        }
    #endif
    */
    // === Draw memory rows
    drawUsageRow("Heap:", heapUsed, heapTotal, true);
    drawUsageRow("PSRAM:", psramUsed, psramTotal);
#ifdef ESP32
    if (flashTotal > 0)
        drawUsageRow("Flash:", flashUsed, flashTotal);
#endif
    if (hasSD && sdTotal > 0)
        drawUsageRow("SD:", sdUsed, sdTotal);
}

#if defined(ESP_PLATFORM) && defined(USE_ST7789)
SPIClass SPI1(HSPI);
#endif

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(32)
{
    graphics::normalFrames = new FrameCallback[MAX_NUM_NODES + NUM_EXTRA_FRAMES];
#if defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
    dispdev = new SH1106Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7789)
#ifdef ESP_PLATFORM
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT, ST7789_SDA,
                            ST7789_MISO, ST7789_SCK);
#else
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
    static_cast<ST7789Spi *>(dispdev)->setRGB(COLOR565(255, 255, 128));
#endif
#elif defined(USE_SSD1306)
    dispdev = new SSD1306Wire(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7789_CS) ||    \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS)
    dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && !defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDisplay(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDynamicDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7567)
    dispdev = new ST7567Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif ARCH_PORTDUINO && !HAS_TFT
    if (settingsMap[displayPanel] != no_screen) {
        LOG_DEBUG("Make TFTDisplay!");
        dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                                 (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
    } else {
        dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                                   (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
        isAUTOOled = true;
    }
#else
    dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                               (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
    isAUTOOled = true;
#endif

    ui = new OLEDDisplayUi(dispdev);
    cmdQueue.setReader(this);
}

Screen::~Screen()
{
    delete[] graphics::normalFrames;
}

/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef USE_EINK
    setOn(false, drawDeepSleepScreen);
#ifdef PIN_EINK_EN
    digitalWrite(PIN_EINK_EN, LOW); // power off backlight
#endif
#else
    // Without E-Ink display:
    setOn(false);
#endif
}

void Screen::handleSetOn(bool on, FrameCallback einkScreensaver)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            LOG_INFO("Turn on screen");
            buttonThread->setScreenFlag(true);
            powerMon->setState(meshtastic_PowerMon_State_Screen_On);
#ifdef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_ALDO2);
#endif
#ifdef HELTEC_TRACKER_V1_X
            uint8_t tft_vext_enabled = digitalRead(VEXT_ENABLE);
#endif
#if !ARCH_PORTDUINO
            dispdev->displayOn();
#endif

#if defined(ST7789_CS) &&                                                                                                        \
    !defined(M5STACK) // set display brightness when turning on screens. Just moved function from TFTDisplay to here.
            static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

            dispdev->displayOn();
#ifdef HELTEC_TRACKER_V1_X
            // If the TFT VEXT power is not enabled, initialize the UI.
            if (!tft_vext_enabled) {
                ui->init();
            }
#endif
#ifdef USE_ST7789
            pinMode(VTFT_CTRL, OUTPUT);
            digitalWrite(VTFT_CTRL, LOW);
            ui->init();
#ifdef ESP_PLATFORM
            analogWrite(VTFT_LEDA, BRIGHTNESS_DEFAULT);
#else
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, TFT_BACKLIGHT_ON);
#endif
#endif
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
#ifdef USE_EINK
            // eInkScreensaver parameter is usually NULL (default argument), default frame used instead
            setScreensaverFrames(einkScreensaver);
#endif
            LOG_INFO("Turn off screen");
            buttonThread->setScreenFlag(false);
#ifdef ELECROW_ThinkNode_M1
            if (digitalRead(PIN_EINK_EN) == HIGH) {
                digitalWrite(PIN_EINK_EN, LOW);
            }
#endif
            dispdev->displayOff();
#ifdef USE_ST7789
            SPI1.end();
#if defined(ARCH_ESP32)
            pinMode(VTFT_LEDA, ANALOG);
            pinMode(VTFT_CTRL, ANALOG);
            pinMode(ST7789_RESET, ANALOG);
            pinMode(ST7789_RS, ANALOG);
            pinMode(ST7789_NSS, ANALOG);
#else
            nrf_gpio_cfg_default(VTFT_LEDA);
            nrf_gpio_cfg_default(VTFT_CTRL);
            nrf_gpio_cfg_default(ST7789_RESET);
            nrf_gpio_cfg_default(ST7789_RS);
            nrf_gpio_cfg_default(ST7789_NSS);
#endif
#endif

#ifdef T_WATCH_S3
            PMU->disablePowerOutput(XPOWERS_ALDO2);
#endif
            enabled = false;
        }
        screenOn = on;
    }
}
static int8_t lastFrameIndex = -1;
static uint32_t lastFrameChangeTime = 0;
constexpr uint32_t ICON_DISPLAY_DURATION_MS = 2000;

void NavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    int currentFrame = state->currentFrame;

    // Detect frame change and record time
    if (currentFrame != lastFrameIndex) {
        lastFrameIndex = currentFrame;
        lastFrameChangeTime = millis();
    }

    const bool useBigIcons = (SCREEN_WIDTH > 128);
    const int iconSize = useBigIcons ? 16 : 8;
    const int spacing = useBigIcons ? 8 : 4;
    const int bigOffset = useBigIcons ? 1 : 0;

    const size_t totalIcons = screen->indicatorIcons.size();
    if (totalIcons == 0) return;

    const size_t iconsPerPage = (SCREEN_WIDTH + spacing) / (iconSize + spacing);
    const size_t currentPage = currentFrame / iconsPerPage;
    const size_t pageStart = currentPage * iconsPerPage;
    const size_t pageEnd = min(pageStart + iconsPerPage, totalIcons);

    const int totalWidth = (pageEnd - pageStart) * iconSize + (pageEnd - pageStart - 1) * spacing;
    const int xStart = (SCREEN_WIDTH - totalWidth) / 2;

    // Only show bar briefly after switching frames (unless on E-Ink)
#if defined(USE_EINK)
    int y = SCREEN_HEIGHT - iconSize - 1;
#else
    int y = SCREEN_HEIGHT - iconSize - 1;
    if (millis() - lastFrameChangeTime > ICON_DISPLAY_DURATION_MS) {
        y = SCREEN_HEIGHT;
    }
#endif

    // Pre-calculate bounding rect
    const int rectX = xStart - 2 - bigOffset;
    const int rectWidth = totalWidth + 4 + (bigOffset * 2);
    const int rectHeight = iconSize + 6;

    // Clear background and draw border
    display->setColor(BLACK);
    display->fillRect(rectX + 1, y - 2, rectWidth - 2, rectHeight - 2);
    display->setColor(WHITE);
    display->drawRect(rectX, y - 2, rectWidth, rectHeight);

    // Icon drawing loop for the current page
    for (size_t i = pageStart; i < pageEnd; ++i) {
        const uint8_t *icon = screen->indicatorIcons[i];
        const int x = xStart + (i - pageStart) * (iconSize + spacing);
        const bool isActive = (i == static_cast<size_t>(currentFrame));

        if (isActive) {
            display->setColor(WHITE);
            display->fillRect(x - 2, y - 2, iconSize + 4, iconSize + 4);
            display->setColor(BLACK);
        }

        if (useBigIcons) {
            drawScaledXBitmap16x16(x, y, 8, 8, icon, display);
        } else {
            display->drawXbm(x, y, iconSize, iconSize, icon);
        }

        if (isActive) {
            display->setColor(WHITE);
        }
    }

    // Knock the corners off the square
    display->setColor(BLACK);
    display->drawRect(rectX, y - 2, 1, 1);
    display->drawRect(rectX + rectWidth - 1, y - 2, 1, 1);
    display->setColor(WHITE);
}

void Screen::setup()
{
    // === Enable display rendering ===
    useDisplay = true;

    // === Detect OLED subtype (if supported by board variant) ===
#ifdef AutoOLEDWire_h
    if (isAUTOOled)
        static_cast<AutoOLEDWire *>(dispdev)->setDetected(model);
#endif

#ifdef USE_SH1107_128_64
    static_cast<SH1106Wire *>(dispdev)->setSubtype(7);
#endif

#if defined(USE_ST7789) && defined(TFT_MESH)
    // Apply custom RGB color (e.g. Heltec T114/T190)
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#endif

    // === Initialize display and UI system ===
    ui->init();
    displayWidth = dispdev->width();
    displayHeight = dispdev->height();

    ui->setTimePerTransition(0);           // Disable animation delays
    ui->setIndicatorPosition(BOTTOM);      // Not used (indicators disabled below)
    ui->setIndicatorDirection(LEFT_RIGHT); // Not used (indicators disabled below)
    ui->setFrameAnimation(SLIDE_LEFT);     // Used only when indicators are active
    ui->disableAllIndicators();            // Disable page indicator dots
    ui->getUiState()->userData = this;     // Allow static callbacks to access Screen instance

    // === Set custom overlay callbacks ===
    static OverlayCallback overlays[] = {
        drawFunctionOverlay, // For mute/buzzer modifiers etc.
        NavigationBar // Custom indicator icons for each frame
    };
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    // === Enable UTF-8 to display mapping ===
    dispdev->setFontTableLookupFunction(customFontTableLookup);

#ifdef USERPREFS_OEM_TEXT
    logo_timeout *= 2; // Give more time for branded boot logos
#endif

    // === Configure alert frames (e.g., "Resuming..." or region name) ===
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip slow refresh
    alertFrames[0] = [this](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
#ifdef ARCH_ESP32
        if (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1)
            drawFrameText(display, state, x, y, "Resuming...");
        else
#endif
        {
            const char *region = myRegion ? myRegion->name : nullptr;
            drawIconScreen(region, display, state, x, y);
        }
    };
    ui->setFrames(alertFrames, 1);
    ui->disableAutoTransition(); // Require manual navigation between frames

    // === Log buffer for on-screen logs (3 lines max) ===
    dispdev->setLogBuffer(3, 32);

    // === Optional screen mirroring or flipping (e.g. for T-Beam orientation) ===
#ifdef SCREEN_MIRROR
    dispdev->mirrorScreen();
#else
    if (!config.display.flip_screen) {
#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7789_CS) ||      \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS)
        static_cast<TFTDisplay *>(dispdev)->flipScreenVertically();
#elif defined(USE_ST7789)
        static_cast<ST7789Spi *>(dispdev)->flipScreenVertically();
#else
        dispdev->flipScreenVertically();
#endif
    }
#endif

    // === Generate device ID from MAC address ===
    uint8_t dmac[6];
    getMacAddr(dmac);
    snprintf(ourId, sizeof(ourId), "%02x%02x", dmac[4], dmac[5]);

#if ARCH_PORTDUINO
    handleSetOn(false); // Ensure proper init for Arduino targets
#endif

    // === Turn on display and trigger first draw ===
    handleSetOn(true);
    ui->update();
#ifndef USE_EINK
    ui->update(); // Some SSD1306 clones drop the first draw, so run twice
#endif
    serialSinceMsec = millis();

    // === Optional touchscreen support ===
#if ARCH_PORTDUINO && !HAS_TFT
    if (settingsMap[touchscreenModule]) {
        touchScreenImpl1 =
            new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
        touchScreenImpl1->init();
    }
#elif HAS_TOUCHSCREEN
    touchScreenImpl1 =
        new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
    touchScreenImpl1->init();
#endif

    // === Subscribe to device status updates ===
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe(adminModule);
#endif
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);
    if (inputBroker)
        inputObserver.observe(inputBroker);

    // === Notify modules that support UI events ===
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay(bool forceUiUpdate)
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    // If requested, make sure queued commands are run, and UI has rendered a new frame
    if (forceUiUpdate) {
        // Force a display refresh, in addition to the UI update
        // Changing the GPS status bar icon apparently doesn't register as a change in image
        // (False negative of the image hashing algorithm used to skip identical frames)
        EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST);

        // No delay between UI frame rendering
        setFastFramerate();

        // Make sure all CMDs have run first
        while (!cmdQueue.isEmpty())
            runOnce();

        // Ensure at least one frame has drawn
        uint64_t startUpdate;
        do {
            startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
            delay(10);
            ui->update();
        } while (ui->getUiState()->lastUpdate < startUpdate);

        // Return to normal frame rate
        targetFramerate = IDLE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }

    // Tell EInk class to update the display
    static_cast<EInkDisplay *>(dispdev)->forceDisplay();
#endif
}

static uint32_t lastScreenTransition;

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    if (displayHeight == 0) {
        displayHeight = dispdev->getHeight();
    }

    // Show boot screen for first logo_timeout seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (logo_timeout + serialSinceMsec))) {
        LOG_INFO("Done with boot screen");
        stopBootScreen();
        showingBootScreen = false;
    }

#ifdef USERPREFS_OEM_TEXT
    static bool showingOEMBootScreen = true;
    if (showingOEMBootScreen && (millis() > ((logo_timeout / 2) + serialSinceMsec))) {
        LOG_INFO("Switch to OEM screen...");
        // Change frames.
        static FrameCallback bootOEMFrames[] = {drawOEMBootScreen};
        static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
        ui->setFrames(bootOEMFrames, bootOEMFrameCount);
        ui->update();
#ifndef USE_EINK
        ui->update();
#endif
        showingOEMBootScreen = false;
    }
#endif

#ifndef DISABLE_WELCOME_UNSET
    if (showingNormalScreen && config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        setWelcomeFrames();
    }
#endif

    // Process incoming commands.
    for (;;) {
        ScreenCmd cmd;
        if (!cmdQueue.dequeue(&cmd, 0)) {
            break;
        }
        switch (cmd.cmd) {
        case Cmd::SET_ON:
            handleSetOn(true);
            break;
        case Cmd::SET_OFF:
            handleSetOn(false);
            break;
        case Cmd::ON_PRESS:
            handleOnPress();
            break;
        case Cmd::SHOW_PREV_FRAME:
            handleShowPrevFrame();
            break;
        case Cmd::SHOW_NEXT_FRAME:
            handleShowNextFrame();
            break;
        case Cmd::START_ALERT_FRAME: {
            showingBootScreen = false; // this should avoid the edge case where an alert triggers before the boot screen goes away
            showingNormalScreen = false;
            alertFrames[0] = alertFrame;
#ifdef USE_EINK
            EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
            EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
            handleSetOn(true); // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
#endif
            setFrameImmediateDraw(alertFrames);
            break;
        }
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_ALERT_FRAME:
        case Cmd::STOP_BOOT_SCREEN:
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // E-Ink: Explicitly use full-refresh for next frame
            setFrames();
            break;
        case Cmd::PRINT:
            handlePrint(cmd.print_text);
            free(cmd.print_text);
            break;
        default:
            LOG_ERROR("Invalid screen cmd");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui->update();

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.

    if (targetFramerate != IDLE_FRAMERATE && ui->getUiState()->frameState == FIXED) {
        // oldFrameState = ui->getUiState()->frameState;
        targetFramerate = IDLE_FRAMERATE;

        ui->setTargetFPS(targetFramerate);
        forceDisplay();
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            !Throttle::isWithinTimespanMs(lastScreenTransition, config.display.auto_screen_carousel_secs * 1000)) {

            // If an E-Ink display struggles with fast refresh, force carousel to use full refresh instead
            // Carousel is potentially a major source of E-Ink display wear
#if !defined(EINK_BACKGROUND_USES_FAST)
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC);
#endif

            LOG_DEBUG("LastScreenTransition exceeded %ums transition to next frame", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // LOG_DEBUG("want fps %d, fixed=%d", targetFramerate,
    // ui->getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
    return (1000 / targetFramerate);
}

void Screen::drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrame(display, state, x, y);
}

void Screen::drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrameSettings(display, state, x, y);
}

void Screen::drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrameWiFi(display, state, x, y);
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setSSLFrames()
{
    if (address_found.address) {
        // LOG_DEBUG("Show SSL frames");
        static FrameCallback sslFrames[] = {drawSSLScreen};
        ui->setFrames(sslFrames, 1);
        ui->update();
    }
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setWelcomeFrames()
{
    if (address_found.address) {
        // LOG_DEBUG("Show Welcome frames");
        static FrameCallback frames[] = {drawWelcomeScreen};
        setFrameImmediateDraw(frames);
    }
}

#ifdef USE_EINK
/// Determine which screensaver frame to use, then set the FrameCallback
void Screen::setScreensaverFrames(FrameCallback einkScreensaver)
{
    // Retain specified frame / overlay callback beyond scope of this method
    static FrameCallback screensaverFrame;
    static OverlayCallback screensaverOverlay;

#if defined(HAS_EINK_ASYNCFULL) && defined(USE_EINK_DYNAMICDISPLAY)
    // Join (await) a currently running async refresh, then run the post-update code.
    // Avoid skipping of screensaver frame. Would otherwise be handled by NotifiedWorkerThread.
    EINK_JOIN_ASYNCREFRESH(dispdev);
#endif

    // If: one-off screensaver frame passed as argument. Handles doDeepSleep()
    if (einkScreensaver != NULL) {
        screensaverFrame = einkScreensaver;
        ui->setFrames(&screensaverFrame, 1);
    }

    // Else, display the usual "overlay" screensaver
    else {
        screensaverOverlay = drawScreensaverOverlay;
        ui->setOverlays(&screensaverOverlay, 1);
    }

    // Request new frame, ASAP
    setFastFramerate();
    uint64_t startUpdate;
    do {
        startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
        delay(1);
        ui->update();
    } while (ui->getUiState()->lastUpdate < startUpdate);

    // Old EInkDisplay class
#if !defined(USE_EINK_DYNAMICDISPLAY)
    static_cast<EInkDisplay *>(dispdev)->forceDisplay(0); // Screen::forceDisplay(), but override rate-limit
#endif

    // Prepare now for next frame, shown when display wakes
    ui->setOverlays(NULL, 0);  // Clear overlay
    setFrames(FOCUS_PRESERVE); // Return to normal display updates, showing same frame as before screensaver, ideally

    // Pick a refresh method, for when display wakes
#ifdef EINK_HASQUIRK_GHOSTING
    EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // Really ugly to see ghosting from "screen paused"
#else
    EINK_ADD_FRAMEFLAG(dispdev, RESPONSIVE); // Really nice to wake screen with a fast-refresh
#endif
}
#endif

// Regenerate the normal set of frames, focusing a specific frame if requested
// Called when a frame should be added / removed, or custom frames should be cleared
void Screen::setFrames(FrameFocus focus)
{
    uint8_t originalPosition = ui->getUiState()->currentFrame;
    FramesetInfo fsi; // Location of specific frames, for applying focus parameter

    LOG_DEBUG("Show standard frames");
    showingNormalScreen = true;

    indicatorIcons.clear();
#ifdef USE_EINK
    // If user has disabled the screensaver, warn them after boot
    static bool warnedScreensaverDisabled = false;
    if (config.display.screen_on_secs == 0 && !warnedScreensaverDisabled) {
        screen->print("Screensaver disabled\n");
        warnedScreensaverDisabled = true;
    }
#endif

    moduleFrames = MeshModule::GetMeshModulesWithUIFrames();
    LOG_DEBUG("Show %d module frames", moduleFrames.size());
#ifdef DEBUG_PORT
    int totalFrameCount = MAX_NUM_NODES + NUM_EXTRA_FRAMES + moduleFrames.size();
    LOG_DEBUG("Total frame count: %d", totalFrameCount);
#endif

    // We don't show the node info of our node (if we have it yet - we should)
    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    if (numMeshNodes > 0)
        numMeshNodes--;

    size_t numframes = 0;

    // put all of the module frames first.
    // this is a little bit of a dirty hack; since we're going to call
    // the same drawModuleFrame handler here for all of these module frames
    // and then we'll just assume that the state->currentFrame value
    // is the same offset into the moduleFrames vector
    // so that we can invoke the module's callback
    for (auto i = moduleFrames.begin(); i != moduleFrames.end(); ++i) {
        // Draw the module frame, using the hack described above
        normalFrames[numframes] = drawModuleFrame;

        // Check if the module being drawn has requested focus
        // We will honor this request later, if setFrames was triggered by a UIFrameEvent
        MeshModule *m = *i;
        if (m->isRequestingFocus())
            fsi.positions.focusedModule = numframes;
        if (m == waypointModule)
            fsi.positions.waypoint = numframes;

        indicatorIcons.push_back(icon_module);
        numframes++;
    }

    LOG_DEBUG("Added modules.  numframes: %d", numframes);

    // If we have a critical fault, show it first
    fsi.positions.fault = numframes;
    if (error_code) {
        normalFrames[numframes++] = drawCriticalFaultFrame;
        indicatorIcons.push_back(icon_error);
        focus = FOCUS_FAULT; // Change our "focus" parameter, to ensure we show the fault frame
    }

#if defined(DISPLAY_CLOCK_FRAME)
    normalFrames[numframes++] = screen->digitalWatchFace ? &Screen::drawDigitalClockFrame : &Screen::drawAnalogClockFrame;
#endif

    // Declare this early so it’s available in FOCUS_PRESERVE block
    bool willInsertTextMessage = shouldDrawMessage(&devicestate.rx_text_message);

    if (willInsertTextMessage) {
        fsi.positions.textMessage = numframes;
        normalFrames[numframes++] = drawTextMessageFrame;
        indicatorIcons.push_back(icon_mail);
    }

    normalFrames[numframes++] = drawDeviceFocused;
    indicatorIcons.push_back(icon_home);

#ifndef USE_EINK
    normalFrames[numframes++] = drawDynamicNodeListScreen;
    indicatorIcons.push_back(icon_nodes);
#endif

// Show detailed node views only on E-Ink builds
#ifdef USE_EINK
    normalFrames[numframes++] = drawLastHeardScreen;
    indicatorIcons.push_back(icon_nodes);

    normalFrames[numframes++] = drawHopSignalScreen;
    indicatorIcons.push_back(icon_signal);

    normalFrames[numframes++] = drawDistanceScreen;
    indicatorIcons.push_back(icon_distance);
#endif

    normalFrames[numframes++] = drawNodeListWithCompasses;
    indicatorIcons.push_back(icon_list);

    normalFrames[numframes++] = drawCompassAndLocationScreen;
    indicatorIcons.push_back(icon_compass);

    normalFrames[numframes++] = drawLoRaFocused;
    indicatorIcons.push_back(icon_radio);

    if (!dismissedFrames.memory) {
        fsi.positions.memory = numframes;
        normalFrames[numframes++] = drawMemoryScreen;
        indicatorIcons.push_back(icon_memory);
    }

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (n && n->num != nodeDB->getNodeNum() && n->is_favorite) {
            normalFrames[numframes++] = drawNodeInfo;
            indicatorIcons.push_back(icon_node);
        }
    }

    // then the debug info

    // Since frames are basic function pointers, we have to use a helper to
    // call a method on debugInfo object.
    // fsi.positions.log = numframes;
    // normalFrames[numframes++] = &Screen::drawDebugInfoTrampoline;

    // call a method on debugInfoScreen object (for more details)
    // fsi.positions.settings = numframes;
    // normalFrames[numframes++] = &Screen::drawDebugInfoSettingsTrampoline;

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (!dismissedFrames.wifi && isWifiAvailable()) {
        fsi.positions.wifi = numframes;
        normalFrames[numframes++] = &Screen::drawDebugInfoWiFiTrampoline;
        indicatorIcons.push_back(icon_wifi);
    }
#endif

    fsi.frameCount = numframes;   // Total framecount is used to apply FOCUS_PRESERVE
    this->frameCount = numframes; // ✅ Save frame count for use in custom overlay
    LOG_DEBUG("Finished build frames. numframes: %d", numframes);

    ui->setFrames(normalFrames, numframes);
    ui->disableAllIndicators();

    // Add overlays: frame icons and alert banner)
    static OverlayCallback overlays[] = {NavigationBar, drawAlertBannerOverlay};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list
                    // just changed)

    // Focus on a specific frame, in the frame set we just created
    switch (focus) {
    case FOCUS_DEFAULT:
        ui->switchToFrame(fsi.positions.deviceFocused);
        break;
    case FOCUS_FAULT:
        ui->switchToFrame(fsi.positions.fault);
        break;
    case FOCUS_TEXTMESSAGE:
        hasUnreadMessage = false; // ✅ Clear when message is *viewed*
        ui->switchToFrame(fsi.positions.textMessage);
        break;
    case FOCUS_MODULE:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.focusedModule);
        break;

    case FOCUS_PRESERVE:
        // No more adjustment — force stay on same index
        if (originalPosition < fsi.frameCount)
            ui->switchToFrame(originalPosition);
        else
            ui->switchToFrame(fsi.frameCount - 1);
        break;
    }

    // Store the info about this frameset, for future setFrames calls
    this->framesetInfo = fsi;

    setFastFramerate(); // Draw ASAP
}

void Screen::setFrameImmediateDraw(FrameCallback *drawFrames)
{
    ui->disableAllIndicators();
    ui->setFrames(drawFrames, 1);
    setFastFramerate();
}

// Dismisses the currently displayed screen frame, if possible
// Relevant for text message, waypoint, others in future?
// Triggered with a CardKB keycombo
void Screen::dismissCurrentFrame()
{
    uint8_t currentFrame = ui->getUiState()->currentFrame;
    bool dismissed = false;

    if (currentFrame == framesetInfo.positions.textMessage && devicestate.has_rx_text_message) {
        LOG_INFO("Dismiss Text Message");
        devicestate.has_rx_text_message = false;
        memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
        dismissedFrames.textMessage = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.waypoint && devicestate.has_rx_waypoint) {
        LOG_DEBUG("Dismiss Waypoint");
        devicestate.has_rx_waypoint = false;
        dismissedFrames.waypoint = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.wifi) {
        LOG_DEBUG("Dismiss WiFi Screen");
        dismissedFrames.wifi = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.memory) {
        LOG_INFO("Dismiss Memory");
        dismissedFrames.memory = true;
        dismissed = true;
    }

    if (dismissed) {
        setFrames(FOCUS_DEFAULT); // You could also use FOCUS_PRESERVE
    }
}

void Screen::handleStartFirmwareUpdateScreen()
{
    LOG_DEBUG("Show firmware screen");
    showingNormalScreen = false;
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // E-Ink: Explicitly use fast-refresh for next frame

    static FrameCallback frames[] = {drawFrameFirmware};
    setFrameImmediateDraw(frames);
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev->setBrightness(254);
    while (count > 0) {
        dispdev->fillRect(0, 0, dispdev->getWidth(), dispdev->getHeight());
        dispdev->display();
        delay(50);
        dispdev->clear();
        dispdev->display();
        delay(50);
        count = count - 1;
    }
    // The dispdev->setBrightness does not work for t-deck display, it seems to run the setBrightness function in OLEDDisplay.
    dispdev->setBrightness(brightness);
}

void Screen::increaseBrightness()
{
    brightness = ((brightness + 62) > 254) ? brightness : (brightness + 62);

#if defined(ST7789_CS)
    // run the setDisplayBrightness function. This works on t-decks
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::decreaseBrightness()
{
    brightness = (brightness < 70) ? brightness : (brightness - 62);

#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::setFunctionSymbol(std::string sym)
{
    if (std::find(functionSymbol.begin(), functionSymbol.end(), sym) == functionSymbol.end()) {
        functionSymbol.push_back(sym);
        functionSymbolString = "";
        for (auto symbol : functionSymbol) {
            functionSymbolString = symbol + " " + functionSymbolString;
        }
        setFastFramerate();
    }
}

void Screen::removeFunctionSymbol(std::string sym)
{
    functionSymbol.erase(std::remove(functionSymbol.begin(), functionSymbol.end(), sym), functionSymbol.end());
    functionSymbolString = "";
    for (auto symbol : functionSymbol) {
        functionSymbolString = symbol + " " + functionSymbolString;
    }
    setFastFramerate();
}

std::string Screen::drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds)
{
    std::string uptime;

    if (days > (hours_in_month * 6))
        uptime = "?";
    else if (days >= 2)
        uptime = std::to_string(days) + "d";
    else if (hours >= 2)
        uptime = std::to_string(hours) + "h";
    else if (minutes >= 1)
        uptime = std::to_string(minutes) + "m";
    else
        uptime = std::to_string(seconds) + "s";
    return uptime;
}

void Screen::handlePrint(const char *text)
{
    // the string passed into us probably has a newline, but that would confuse the logging system
    // so strip it
    LOG_DEBUG("Screen: %.*s", strlen(text) - 1, text);
    if (!useDisplay || !showingNormalScreen)
        return;

    dispdev->print(text);
}

void Screen::handleOnPress()
{
    // If Canned Messages is using the "Scan and Select" input, dismiss the canned message frame when user button is pressed
    // Minimize impact as a courtesy, as "scan and select" may be used as default config for some boards
    if (scanAndSelectInput != nullptr && scanAndSelectInput->dismissCannedMessageFrame())
        return;

    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowPrevFrame()
{
    // If screen was off, just wake it, otherwise go back to previous frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->previousFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowNextFrame()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

    ui->setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}

void DebugInfo::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char channelStr[20];
    {
        concurrency::LockGuard guard(&lock);
        snprintf(channelStr, sizeof(channelStr), "#%s", channels.getName(channels.getPrimaryIndex()));
    }

    // Display power status
    if (powerStatus->getHasBattery()) {
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
            drawBattery(display, x, y + 2, imgBattery, powerStatus);
        } else {
            drawBattery(display, x + 1, y + 3, imgBattery, powerStatus);
        }
    } else if (powerStatus->knowsUSB()) {
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
            display->drawFastImage(x, y + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
        } else {
            display->drawFastImage(x + 1, y + 3, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
        }
    }
    // Display nodes status
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 2, nodeStatus);
    } else {
        drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 3, nodeStatus);
    }
#if HAS_GPS
    // Display GPS status
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        drawGPSpowerstat(display, x, y + 2, gpsStatus);
    } else {
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
            drawGPS(display, x + (SCREEN_WIDTH * 0.63), y + 2, gpsStatus);
        } else {
            drawGPS(display, x + (SCREEN_WIDTH * 0.63), y + 3, gpsStatus);
        }
    }
#endif
    display->setColor(WHITE);
    // Draw the channel name
    display->drawString(x, y + FONT_HEIGHT_SMALL, channelStr);
    // Draw our hardware ID to assist with bluetooth pairing. Either prefix with Info or S&F Logo
    if (moduleConfig.store_forward.enabled) {
#ifdef ARCH_ESP32
        if (!Throttle::isWithinTimespanMs(storeForwardModule->lastHeartbeat,
                                          (storeForwardModule->heartbeatInterval * 1200))) { // no heartbeat, overlap a bit
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(HX8357_CS) || defined(ILI9488_CS) || ARCH_PORTDUINO) &&                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(ourId), y + 3 + FONT_HEIGHT_SMALL, 12, 8,
                                   imgQuestionL1);
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(ourId), y + 11 + FONT_HEIGHT_SMALL, 12, 8,
                                   imgQuestionL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(ourId), y + 2 + FONT_HEIGHT_SMALL, 8, 8,
                                   imgQuestion);
#endif
        } else {
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS)) &&                                  \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(ourId), y + 3 + FONT_HEIGHT_SMALL, 16, 8,
                                   imgSFL1);
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(ourId), y + 11 + FONT_HEIGHT_SMALL, 16, 8,
                                   imgSFL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 13 - display->getStringWidth(ourId), y + 2 + FONT_HEIGHT_SMALL, 11, 8,
                                   imgSF);
#endif
        }
#endif
    } else {
        // TODO: Raspberry Pi supports more than just the one screen size
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(HX8357_CS) || defined(ILI9488_CS) || ARCH_PORTDUINO) &&                \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(ourId), y + 3 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL1);
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(ourId), y + 11 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL2);
#else
        display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(ourId), y + 2 + FONT_HEIGHT_SMALL, 8, 8, imgInfo);
#endif
    }

    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(ourId), y + FONT_HEIGHT_SMALL, ourId);

    // Draw any log messages
    display->drawLogBuffer(x, y + (FONT_HEIGHT_SMALL * 2));

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

// Jm
void DebugInfo::drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    const char *wifiName = config.network.wifi_ssid;

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    if (WiFi.status() != WL_CONNECTED) {
        display->drawString(x, y, String("WiFi: Not Connected"));
        if (config.display.heading_bold)
            display->drawString(x + 1, y, String("WiFi: Not Connected"));
    } else {
        display->drawString(x, y, String("WiFi: Connected"));
        if (config.display.heading_bold)
            display->drawString(x + 1, y, String("WiFi: Connected"));

        display->drawString(x + SCREEN_WIDTH - display->getStringWidth("RSSI " + String(WiFi.RSSI())), y,
                            "RSSI " + String(WiFi.RSSI()));
        if (config.display.heading_bold) {
            display->drawString(x + SCREEN_WIDTH - display->getStringWidth("RSSI " + String(WiFi.RSSI())) - 1, y,
                                "RSSI " + String(WiFi.RSSI()));
        }
    }

    display->setColor(WHITE);

    /*
    - WL_CONNECTED: assigned when connected to a WiFi network;
    - WL_NO_SSID_AVAIL: assigned when no SSID are available;
    - WL_CONNECT_FAILED: assigned when the connection fails for all the attempts;
    - WL_CONNECTION_LOST: assigned when the connection is lost;
    - WL_DISCONNECTED: assigned when disconnected from a network;
    - WL_IDLE_STATUS: it is a temporary status assigned when WiFi.begin() is called and remains active until the number of
    attempts expires (resulting in WL_CONNECT_FAILED) or a connection is established (resulting in WL_CONNECTED);
    - WL_SCAN_COMPLETED: assigned when the scan networks is completed;
    - WL_NO_SHIELD: assigned when no WiFi shield is present;

    */
    if (WiFi.status() == WL_CONNECTED) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IP: " + String(WiFi.localIP().toString().c_str()));
    } else if (WiFi.status() == WL_NO_SSID_AVAIL) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "SSID Not Found");
    } else if (WiFi.status() == WL_CONNECTION_LOST) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Connection Lost");
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Connection Failed");
    } else if (WiFi.status() == WL_IDLE_STATUS) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Idle ... Reconnecting");
    }
#ifdef ARCH_ESP32
    else {
        // Codes:
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1,
                            WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(getWifiDisconnectReason())));
    }
#else
    else {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Unkown status: " + String(WiFi.status()));
    }
#endif

    display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "SSID: " + String(wifiName));

    display->drawString(x, y + FONT_HEIGHT_SMALL * 3, "http://meshtastic.local");

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
#endif
}

void DebugInfo::drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "B %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');

        // Line 1
        display->drawString(x, y, batStr);
        if (config.display.heading_bold)
            display->drawString(x + 1, y, batStr);
    } else {
        // Line 1
        display->drawString(x, y, String("USB"));
        if (config.display.heading_bold)
            display->drawString(x + 1, y, String("USB"));
    }

    //    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, true);

    //    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode), y, mode);
    //    if (config.display.heading_bold)
    //        display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode) - 1, y, mode);

    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    // currentMillis %= 1000;
    // seconds %= 60;
    // minutes %= 60;
    // hours %= 24;

    // Show uptime as days, hours, minutes OR seconds
    std::string uptime = screen->drawTimeDelta(days, hours, minutes, seconds);

    // Line 1 (Still)
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());
    if (config.display.heading_bold)
        display->drawString(x - 1 + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());

    display->setColor(WHITE);

    // Setup string to assemble analogClock string
    std::string analogClock = "";

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        char timebuf[12];

        if (config.display.use_12h_clock) {
            std::string meridiem = "am";
            if (hour >= 12) {
                if (hour > 12)
                    hour -= 12;
                meridiem = "pm";
            }
            if (hour == 00) {
                hour = 12;
            }
            snprintf(timebuf, sizeof(timebuf), "%d:%02d:%02d%s", hour, min, sec, meridiem.c_str());
        } else {
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
        }
        analogClock += timebuf;
    }

    // Line 2
    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, analogClock.c_str());

    // Display Channel Utilization
    char chUtil[13];
    snprintf(chUtil, sizeof(chUtil), "ChUtil %2.0f%%", airTime->channelUtilizationPercent());
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(chUtil), y + FONT_HEIGHT_SMALL * 1, chUtil);

#if HAS_GPS
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        // Line 3
        if (config.display.gps_format !=
            meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DMS) // if DMS then don't draw altitude
            drawGPSAltitude(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);

        // Line 4
        drawGPScoordinates(display, x, y + FONT_HEIGHT_SMALL * 3, gpsStatus);
    } else {
        drawGPSpowerstat(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);
    }
#endif
/* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // LOG_DEBUG("Screen got status update %d", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(FOCUS_PRESERVE); // Regen the list of screen frames (returning to same frame, if possible)
        }
        nodeDB->updateGUI = false;
        break;
    }

    return 0;
}

// Handles when message is received; will jump to text message frame.
int Screen::handleTextMessage(const meshtastic_MeshPacket *packet)
{
    if (showingNormalScreen) {
        if (packet->from == 0) {
            // Outgoing message (likely sent from phone)
            devicestate.has_rx_text_message = false;
            memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
            dismissedFrames.textMessage = true;
            hasUnreadMessage = false; // Clear unread state when user replies

            setFrames(FOCUS_PRESERVE); // Stay on same frame, silently update frame list
        } else {
            // Incoming message
            devicestate.has_rx_text_message = true; // Needed to include the message frame
            hasUnreadMessage = true;                // Enables mail icon in the header
            setFrames(FOCUS_PRESERVE);              // Refresh frame list without switching view
            forceDisplay();                         // Forces screen redraw

            // === Prepare banner content ===
            const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet->from);
            const char *longName = (node && node->has_user) ? node->user.long_name : nullptr;

            const char *msgRaw = reinterpret_cast<const char *>(packet->decoded.payload.bytes);
            String msg = String(msgRaw);
            msg.trim(); // Remove leading/trailing whitespace/newlines

            String banner;

            // Match bell character or exact alert text
            if (msg.indexOf("\x07") != -1) {
                banner = "Alert Received";
            } else {
                banner = "New Message";
            }

            if (longName && longName[0]) {
                banner += "\nfrom ";
                banner += longName;
            }

            screen->showOverlayBanner(banner, 3000);
        }
    }

    return 0;
}

// Triggered by MeshModules
int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    if (showingNormalScreen) {
        // Regenerate the frameset, potentially honoring a module's internal requestFocus() call
        if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET)
            setFrames(FOCUS_MODULE);

        // Regenerate the frameset, while Attempt to maintain focus on the current frame
        else if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND)
            setFrames(FOCUS_PRESERVE);

        // Don't regenerate the frameset, just re-draw whatever is on screen ASAP
        else if (event->action == UIFrameEvent::Action::REDRAW_ONLY)
            setFastFramerate();
    }

    return 0;
}

int Screen::handleInputEvent(const InputEvent *event)
{

#if defined(DISPLAY_CLOCK_FRAME)
    // For the T-Watch, intercept touches to the 'toggle digital/analog watch face' button
    uint8_t watchFaceFrame = error_code ? 1 : 0;

    if (this->ui->getUiState()->currentFrame == watchFaceFrame && event->touchX >= 204 && event->touchX <= 240 &&
        event->touchY >= 204 && event->touchY <= 240) {
        screen->digitalWatchFace = !screen->digitalWatchFace;

        setFrames();

        return 0;
    }
#endif

    // Use left or right input from a keyboard to move between frames,
    // so long as a mesh module isn't using these events for some other purpose
    if (showingNormalScreen) {

        // Ask any MeshModules if they're handling keyboard input right now
        bool inputIntercepted = false;
        for (MeshModule *module : moduleFrames) {
            if (module->interceptingKeyboardInput())
                inputIntercepted = true;
        }

        // If no modules are using the input, move between frames
        if (!inputIntercepted) {
            if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT))
                showPrevFrame();
            else if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT))
                showNextFrame();
        }
    }

    return 0;
}

int Screen::handleAdminMessage(const meshtastic_AdminMessage *arg)
{
    switch (arg->which_payload_variant) {
    // Node removed manually (i.e. via app)
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        setFrames(FOCUS_PRESERVE);
        break;

    // Default no-op, in case the admin message observable gets used by other classes in future
    default:
        break;
    }
    return 0;
}

} // namespace graphics
#else
graphics::Screen::Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY) {}
#endif // HAS_SCREEN
