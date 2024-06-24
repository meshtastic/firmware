/*

SSD1306 - Screen module

Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>


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
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>

#include "DisplayFormatters.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "NodeDB.h"
#include "error.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/images.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "meshUtils.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "modules/WaypointModule.h"
#include "sleep.h"
#include "target_specific.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#include "modules/esp32/StoreForwardModule.h"
#endif

#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

/// Convert an integer GPS coords to a floating point
#define DegD(i) (i * 1e-7)

using namespace meshtastic; /** @todo remove */

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
static char btPIN[16] = "888888";

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
std::vector<std::string> functionSymbals;
// string displayed in bottom right corner of display. Created from elements in functionSymbals vector
std::string functionSymbalString = "";

#if HAS_GPS
// GeoCoord object for the screen
GeoCoord geoCoord;
#endif

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

static uint16_t displayWidth, displayHeight;

#define SCREEN_WIDTH displayWidth
#define SCREEN_HEIGHT displayHeight

#include "graphics/ScreenFonts.h"

#define getStringCenteredX(s) ((SCREEN_WIDTH - display->getStringWidth(s)) / 2)

/**
 * Draw the icon with extra info printed around the corners
 */
static void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
    display->drawXbm(x + (SCREEN_WIDTH - icon_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2,
                     icon_width, icon_height, icon_bits);

    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = "meshtastic.org";
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version in upper right
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION_SHORT)); // Note: we don't bother printing region or now, it makes the string too long
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(buf), y + 0, buf);
    screen->forceDisplay();
    // FIXME - draw serial # somewhere?
}

static void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
    display->drawXbm(x + (SCREEN_WIDTH - oemStore.oem_icon_width) / 2,
                     y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - oemStore.oem_icon_height) / 2 + 2, oemStore.oem_icon_width,
                     oemStore.oem_icon_height, (const uint8_t *)oemStore.oem_icon_bits.bytes);

    switch (oemStore.oem_font) {
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
    const char *title = oemStore.oem_text;
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version in upper right
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION_SHORT)); // Note: we don't bother printing region or now, it makes the string too long
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(buf), y + 0, buf);
    screen->forceDisplay();

    // FIXME - draw serial # somewhere?
}

static void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

static void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *message)
{
    uint16_t x_offset = display->width() / 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, 26 + y, message);
}

static void drawBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#ifdef ARCH_ESP32
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
        drawFrameText(display, state, x, y, "Resuming...");
    } else
#endif
    {
        // Draw region in upper left
        const char *region = myRegion ? myRegion->name : NULL;
        drawIconScreen(region, display, state, x, y);
    }
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

// draw overlay in bottom right corner of screen to show when notifications are muted or modifier key is active
static void drawFunctionOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // LOG_DEBUG("Drawing function overlay\n");
    if (functionSymbals.begin() != functionSymbals.end()) {
        char buf[64];
        display->setFont(FONT_SMALL);
        snprintf(buf, sizeof(buf), "%s", functionSymbalString.c_str());
        display->drawString(SCREEN_WIDTH - display->getStringWidth(buf), SCREEN_HEIGHT - FONT_HEIGHT_SMALL, buf);
    }
}

/// Check if the display can render a string (detect special chars; emoji)
static bool haveGlyphs(const char *str)
{
#if defined(OLED_UA) || defined(OLED_RU)
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

    LOG_DEBUG("haveGlyphs=%d\n", have);
    return have;
}

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
static void drawDeepSleepScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Drawing deep sleep screen\n");
    drawIconScreen("Sleeping...", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
static void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Drawing screensaver overlay\n");

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
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // LOG_DEBUG("Screen is not in transition.  Frame: %d\n\n", module_frame);
    }
    // LOG_DEBUG("Drawing Module Frame %d\n\n", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

static void drawFrameBluetooth(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    int x_offset = display->width() / 2;
    int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, y_offset + y, "Bluetooth");

    display->setFont(FONT_SMALL);
    y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_MEDIUM - 4 : y_offset + FONT_HEIGHT_MEDIUM + 5;
    display->drawString(x_offset + x, y_offset + y, "Enter this code");

    display->setFont(FONT_LARGE);
    String displayPin(btPIN);
    String pin = displayPin.substring(0, 3) + " " + displayPin.substring(3, 6);
    y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_SMALL - 5 : y_offset + FONT_HEIGHT_SMALL + 5;
    display->drawString(x_offset + x, y_offset + y, pin);

    display->setFont(FONT_SMALL);
    String deviceName = "Name: ";
    deviceName.concat(getDeviceName());
    y_offset = display->height() == 64 ? y_offset + FONT_HEIGHT_LARGE - 6 : y_offset + FONT_HEIGHT_LARGE + 5;
    display->drawString(x_offset + x, y_offset + y, deviceName);
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

// Determine whether the waypoint frame should be drawn (waypoint deleted? expired?)
static bool shouldDrawWaypoint(const meshtastic_MeshPacket *packet)
{
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    // If no waypoint to show
    if (!devicestate.has_rx_waypoint)
        return false;

    // Decode the message, to find the expiration time (is waypoint still valid)
    // This handles "deletion" as well as expiration
    meshtastic_Waypoint wp;
    memset(&wp, 0, sizeof(wp));
    if (pb_decode_from_bytes(packet->decoded.payload.bytes, packet->decoded.payload.size, &meshtastic_Waypoint_msg, &wp)) {
        // Valid waypoint
        if (wp.expire > getTime())
            return devicestate.has_rx_waypoint = true;

        // Expired, or deleted
        else
            return devicestate.has_rx_waypoint = false;
    }

    // If decoding failed
    LOG_ERROR("Failed to decode waypoint\n");
    devicestate.has_rx_waypoint = false;
    return false;
#else
    return false;
#endif
}

// Draw power bars or a charging indicator on an image of a battery, determined by battery charge voltage or percentage.
static void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const PowerStatus *powerStatus)
{
    static const uint8_t powerBar[3] = {0x81, 0xBD, 0xBD};
    static const uint8_t lightning[8] = {0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85};
    // Clear the bar area on the battery image
    for (int i = 1; i < 14; i++) {
        imgBuffer[i] = 0x81;
    }
    // If charging, draw a charging indicator
    if (powerStatus->getIsCharging()) {
        memcpy(imgBuffer + 3, lightning, 8);
        // If not charging, Draw power bars
    } else {
        for (int i = 0; i < 4; i++) {
            if (powerStatus->getBatteryChargePercent() >= 25 * i)
                memcpy(imgBuffer + 1 + (i * 3), powerBar, 3);
        }
    }
    display->drawFastImage(x, y, 16, 8, imgBuffer);
}

#ifdef T_WATCH_S3

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

    if (nimbleBluetooth->isConnected()) {
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

    if (nimbleBluetooth->isConnected()) {
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

/// Draw the last text message we received
static void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // the max length of this buffer is much longer than we can possibly print
    static char tempBuf[237];

    const meshtastic_MeshPacket &mp = devicestate.rx_text_message;
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(&mp));
    // LOG_DEBUG("drawing text message from 0x%x: %s\n", mp.from,
    // mp.decoded.variant.data.decoded.bytes);

    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will
    // be wrapped. Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    // For time delta
    uint32_t seconds = sinceReceived(&mp);
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;

    // For timestamp
    uint8_t timestampHours, timestampMinutes;
    int32_t daysAgo;
    bool useTimestamp = deltaToTimestamp(seconds, &timestampHours, &timestampMinutes, &daysAgo);

    // If bold, draw twice, shifting right by one pixel
    for (uint8_t xOff = 0; xOff <= (config.display.heading_bold ? 1 : 0); xOff++) {
        // Show a timestamp if received today, but longer than 15 minutes ago
        if (useTimestamp && minutes >= 15 && daysAgo == 0) {
            display->drawStringf(xOff + x, 0 + y, tempBuf, "At %02hu:%02hu from %s", timestampHours, timestampMinutes,
                                 (node && node->has_user) ? node->user.short_name : "???");
        }
        // Timestamp yesterday (if display is wide enough)
        else if (useTimestamp && daysAgo == 1 && display->width() >= 200) {
            display->drawStringf(xOff + x, 0 + y, tempBuf, "Yesterday %02hu:%02hu from %s", timestampHours, timestampMinutes,
                                 (node && node->has_user) ? node->user.short_name : "???");
        }
        // Otherwise, show a time delta
        else {
            display->drawStringf(xOff + x, 0 + y, tempBuf, "%s ago from %s",
                                 screen->drawTimeDelta(days, hours, minutes, seconds).c_str(),
                                 (node && node->has_user) ? node->user.short_name : "???");
        }
    }

    display->setColor(WHITE);
#ifndef EXCLUDE_EMOJI
    if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F44D") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - thumbs_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - thumbs_height) / 2 + 2 + 5, thumbs_width, thumbs_height,
                         thumbup);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F44E") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - thumbs_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - thumbs_height) / 2 + 2 + 5, thumbs_width, thumbs_height,
                         thumbdown);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"❓") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - question_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - question_height) / 2 + 2 + 5, question_width, question_height,
                         question);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"‼️") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - bang_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - bang_height) / 2 + 2 + 5,
                         bang_width, bang_height, bang);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F4A9") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - poo_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - poo_height) / 2 + 2 + 5,
                         poo_width, poo_height, poo);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), "\xf0\x9f\xa4\xa3") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - haha_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - haha_height) / 2 + 2 + 5,
                         haha_width, haha_height, haha);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F44B") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - wave_icon_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - wave_icon_height) / 2 + 2 + 5, wave_icon_width,
                         wave_icon_height, wave_icon);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F920") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - cowboy_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - cowboy_height) / 2 + 2 + 5, cowboy_width, cowboy_height,
                         cowboy);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\U0001F42D") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - deadmau5_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - deadmau5_height) / 2 + 2 + 5, deadmau5_width, deadmau5_height,
                         deadmau5);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\xE2\x98\x80\xEF\xB8\x8F") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - sun_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - sun_height) / 2 + 2 + 5,
                         sun_width, sun_height, sun);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\u2614") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - rain_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - rain_height) / 2 + 2 + 10,
                         rain_width, rain_height, rain);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"☁️") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - cloud_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - cloud_height) / 2 + 2 + 5, cloud_width, cloud_height, cloud);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"🌫️") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - fog_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - fog_height) / 2 + 2 + 5,
                         fog_width, fog_height, fog);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"\xf0\x9f\x98\x88") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - devil_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - devil_height) / 2 + 2 + 5, devil_width, devil_height, devil);
    } else if (strcmp(reinterpret_cast<const char *>(mp.decoded.payload.bytes), u8"♥️") == 0) {
        display->drawXbm(x + (SCREEN_WIDTH - heart_width) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - heart_height) / 2 + 2 + 5, heart_width, heart_height, heart);
    } else {
        snprintf(tempBuf, sizeof(tempBuf), "%s", mp.decoded.payload.bytes);
        display->drawStringMaxWidth(0 + x, 0 + y + FONT_HEIGHT_SMALL, x + display->getWidth(), tempBuf);
    }
#else
    snprintf(tempBuf, sizeof(tempBuf), "%s", mp.decoded.payload.bytes);
    display->drawStringMaxWidth(0 + x, 0 + y + FONT_HEIGHT_SMALL, x + display->getWidth(), tempBuf);
#endif
}

/// Draw a series of fields in a column, wrapping to multiple columns if needed
static void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
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
static void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const NodeStatus *nodeStatus)
{
    char usersString[20];
    snprintf(usersString, sizeof(usersString), "%d/%d", nodeStatus->getNumOnline(), nodeStatus->getNumTotal());
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7735_CS) || defined(ST7789_CS) || defined(HX8357_CS)) &&          \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
    display->drawFastImage(x, y + 3, 8, 8, imgUser);
#else
    display->drawFastImage(x, y, 8, 8, imgUser);
#endif
    display->drawString(x + 10, y - 2, usersString);
    if (config.display.heading_bold)
        display->drawString(x + 11, y - 2, usersString);
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
    display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gps->getHasLock()) {
        display->drawString(x + 8, y - 2, "No sats");
        if (config.display.heading_bold)
            display->drawString(x + 9, y - 2, "No sats");
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
            // bar[1] = bar[0];
            display->drawFastImage(x + 9 + (i * 2), y, 2, 8, bar);
        }

        // Draw satellite image
        display->drawFastImage(x + 24, y, 8, 8, imgSatellite);

        // Draw the number of satellites
        snprintf(satsString, sizeof(satsString), "%u", gps->getNumSatellites());
        display->drawString(x + 34, y - 2, satsString);
        if (config.display.heading_bold)
            display->drawString(x + 35, y - 2, satsString);
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
namespace
{

/// A basic 2D point class for drawing
class Point
{
  public:
    float x, y;

    Point(float _x, float _y) : x(_x), y(_y) {}

    /// Apply a rotation around zero (standard rotation matrix math)
    void rotate(float radian)
    {
        float cos = cosf(radian), sin = sinf(radian);
        float rx = x * cos + y * sin, ry = -x * sin + y * cos;

        x = rx;
        y = ry;
    }

    void translate(int16_t dx, int dy)
    {
        x += dx;
        y += dy;
    }

    void scale(float f)
    {
        // We use -f here to counter the flip that happens
        // on the y axis when drawing and rotating on screen
        x *= f;
        y *= -f;
    }
};

} // namespace

static void drawLine(OLEDDisplay *d, const Point &p1, const Point &p2)
{
    d->drawLine(p1.x, p1.y, p2.x, p2.y);
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
static float estimatedHeading(double lat, double lon)
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

static uint16_t getCompassDiam(OLEDDisplay *display)
{
    uint16_t diam = 0;
    uint16_t offset = 0;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT)
        offset = FONT_HEIGHT_SMALL;

    // get the smaller of the 2 dimensions and subtract 20
    if (display->getWidth() > (display->getHeight() - offset)) {
        diam = display->getHeight() - offset;
        // if 2/3 of the other size would be smaller, use that
        if (diam > (display->getWidth() * 2 / 3)) {
            diam = display->getWidth() * 2 / 3;
        }
    } else {
        diam = display->getWidth();
        if (diam > ((display->getHeight() - offset) * 2 / 3)) {
            diam = (display->getHeight() - offset) * 2 / 3;
        }
    }

    return diam - 20;
};

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static size_t nodeIndex;
static int8_t prevFrame = -1;

// Draw the arrow pointing to a node's location
static void drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, float headingRadian)
{
    Point tip(0.0f, 0.5f), tail(0.0f, -0.5f); // pointing up initially
    float arrowOffsetX = 0.2f, arrowOffsetY = 0.2f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y - arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y - arrowOffsetY);

    Point *arrowPoints[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++) {
        arrowPoints[i]->rotate(headingRadian);
        arrowPoints[i]->scale(getCompassDiam(display) * 0.6);
        arrowPoints[i]->translate(compassX, compassY);
    }
    drawLine(display, tip, tail);
    drawLine(display, leftArrow, tip);
    drawLine(display, rightArrow, tip);
}

// Draw north
static void drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading)
{
    // If north is supposed to be at the top of the compass we want rotation to be +0
    if (config.display.compass_north_top)
        myHeading = -0;

    Point N1(-0.04f, 0.65f), N2(0.04f, 0.65f);
    Point N3(-0.04f, 0.55f), N4(0.04f, 0.55f);
    Point *rosePoints[] = {&N1, &N2, &N3, &N4};

    for (int i = 0; i < 4; i++) {
        // North on compass will be negative of heading
        rosePoints[i]->rotate(-myHeading);
        rosePoints[i]->scale(getCompassDiam(display));
        rosePoints[i]->translate(compassX, compassY);
    }
    drawLine(display, N1, N3);
    drawLine(display, N2, N4);
    drawLine(display, N1, N4);
}

// Get a string representation of the time passed since something happened
static void getTimeAgoStr(uint32_t agoSecs, char *timeStr, uint8_t maxLength)
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

static void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // We only advance our nodeIndex if the frame # has changed - because
    // drawNodeInfo will be called repeatedly while the frame is shown
    if (state->currentFrame != prevFrame) {
        prevFrame = state->currentFrame;

        nodeIndex = (nodeIndex + 1) % nodeDB->getNumMeshNodes();
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(nodeIndex);
        if (n->num == nodeDB->getNodeNum()) {
            // Don't show our node, just skip to next
            nodeIndex = (nodeIndex + 1) % nodeDB->getNumMeshNodes();
            n = nodeDB->getMeshNodeByIndex(nodeIndex);
        }
    }

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(nodeIndex);

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
    }

    const char *username = node->has_user ? node->user.long_name : "Unknown Name";

    static char signalStr[20];

    // section here to choose whether to display hops away rather than signal strength if more than 0 hops away.
    if (node->hops_away > 0) {
        snprintf(signalStr, sizeof(signalStr), "Hops Away: %d", node->hops_away);
    } else {
        snprintf(signalStr, sizeof(signalStr), "Signal: %d%%", clamp((int)((node->snr + 10) * 5), 0, 100));
    }

    static char lastStr[20];
    getTimeAgoStr(sinceLastSeen(node), lastStr, sizeof(lastStr));

    static char distStr[20];
    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
        strncpy(distStr, "? mi", sizeof(distStr)); // might not have location data
    } else {
        strncpy(distStr, "? km", sizeof(distStr));
    }
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const char *fields[] = {username, lastStr, signalStr, distStr, NULL};
    int16_t compassX = 0, compassY = 0;

    // coordinates for the center of the compass/circle
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + SCREEN_WIDTH - getCompassDiam(display) / 2 - 5;
        compassY = y + SCREEN_HEIGHT / 2;
    } else {
        compassX = x + SCREEN_WIDTH - getCompassDiam(display) / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT - FONT_HEIGHT_SMALL) / 2;
    }
    bool hasNodeHeading = false;

    if (ourNode && (hasValidPosition(ourNode) || screen->hasHeading())) {
        const meshtastic_PositionLite &op = ourNode->position;
        float myHeading;
        if (screen->hasHeading())
            myHeading = (screen->getHeading()) * PI / 180; // gotta convert compass degrees to Radians
        else
            myHeading = estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        drawCompassNorth(display, compassX, compassY, myHeading);

        if (hasValidPosition(node)) {
            // display direction toward node
            hasNodeHeading = true;
            const meshtastic_PositionLite &p = node->position;
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));

            if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
                if (d < (2 * MILES_TO_FEET))
                    snprintf(distStr, sizeof(distStr), "%.0f ft", d * METERS_TO_FEET);
                else
                    snprintf(distStr, sizeof(distStr), "%.1f mi", d * METERS_TO_FEET / MILES_TO_FEET);
            } else {
                if (d < 2000)
                    snprintf(distStr, sizeof(distStr), "%.0f m", d);
                else
                    snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);
            }

            float bearingToOther =
                GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            // If the top of the compass is a static north then bearingToOther can be drawn on the compass directly
            // If the top of the compass is not a static north we need adjust bearingToOther based on heading
            if (!config.display.compass_north_top)
                bearingToOther -= myHeading;
            drawNodeHeading(display, compassX, compassY, bearingToOther);
        }
    }
    if (!hasNodeHeading) {
        // direction to node is unknown so display question mark
        // Debug info for gps lock errors
        // LOG_DEBUG("ourNode %d, ourPos %d, theirPos %d\n", !!ourNode, ourNode && hasValidPosition(ourNode),
        // hasValidPosition(node));
        display->drawString(compassX - FONT_HEIGHT_SMALL / 4, compassY - FONT_HEIGHT_SMALL / 2, "?");
    }
    display->drawCircle(compassX, compassY, getCompassDiam(display) / 2);

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }
    // Must be after distStr is populated
    drawColumns(display, x, y, fields);
}

/// Draw the last waypoint we received
static void drawWaypointFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Prepare to draw
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Handle inverted display
    // Unsure of expected behavior: for now, copy drawNodeInfo
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);

    // Decode the waypoint
    meshtastic_MeshPacket &mp = devicestate.rx_waypoint;
    meshtastic_Waypoint wp;
    memset(&wp, 0, sizeof(wp));
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Waypoint_msg, &wp)) {
        // This *should* be caught by shouldDrawWaypoint, but we'll short-circuit here just in case
        display->drawStringMaxWidth(0 + x, 0 + y, x + display->getWidth(), "Couldn't decode waypoint");
        devicestate.has_rx_waypoint = false;
        return;
    }

    // Get timestamp info. Will pass as a field to drawColumns
    static char lastStr[20];
    getTimeAgoStr(sinceReceived(&mp), lastStr, sizeof(lastStr));

    // Will contain distance information, passed as a field to drawColumns
    static char distStr[20];

    // Get our node, to use our own position
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());

    // Text fields to draw (left of compass)
    // Last element must be NULL. This signals the end of the char*[] to drawColumns
    const char *fields[] = {"Waypoint", lastStr, wp.name, distStr, NULL};

    // Co-ordinates for the center of the compass/circle
    int16_t compassX = 0, compassY = 0;
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + SCREEN_WIDTH - getCompassDiam(display) / 2 - 5;
        compassY = y + SCREEN_HEIGHT / 2;
    } else {
        compassX = x + SCREEN_WIDTH - getCompassDiam(display) / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT - FONT_HEIGHT_SMALL) / 2;
    }

    // If our node has a position:
    if (ourNode && (hasValidPosition(ourNode) || screen->hasHeading())) {
        const meshtastic_PositionLite &op = ourNode->position;
        float myHeading;
        if (screen->hasHeading())
            myHeading = (screen->getHeading()) * PI / 180; // gotta convert compass degrees to Radians
        else
            myHeading = estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        drawCompassNorth(display, compassX, compassY, myHeading);

        // Distance to Waypoint
        float d = GeoCoord::latLongToMeter(DegD(wp.latitude_i), DegD(wp.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            if (d < (2 * MILES_TO_FEET))
                snprintf(distStr, sizeof(distStr), "%.0f ft", d * METERS_TO_FEET);
            else
                snprintf(distStr, sizeof(distStr), "%.1f mi", d * METERS_TO_FEET / MILES_TO_FEET);
        } else {
            if (d < 2000)
                snprintf(distStr, sizeof(distStr), "%.0f m", d);
            else
                snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);
        }

        // Compass bearing to waypoint
        float bearingToOther =
            GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(wp.latitude_i), DegD(wp.longitude_i));
        // If the top of the compass is a static north then bearingToOther can be drawn on the compass directly
        // If the top of the compass is not a static north we need adjust bearingToOther based on heading
        if (!config.display.compass_north_top)
            bearingToOther -= myHeading;
        drawNodeHeading(display, compassX, compassY, bearingToOther);
    }

    // If our node doesn't have position
    else {
        // ? in the compass
        display->drawString(compassX - FONT_HEIGHT_SMALL / 4, compassY - FONT_HEIGHT_SMALL / 2, "?");

        // ? in the distance field
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            strncpy(distStr, "? mi", sizeof(distStr));
        else
            strncpy(distStr, "? km", sizeof(distStr));
    }

    // Undo color-inversion, if set prior to drawing header
    // Unsure of expected behavior? For now: copy drawNodeInfo
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    // Draw compass circle
    display->drawCircle(compassX, compassY, getCompassDiam(display) / 2);

    // Must be after distStr is populated
    drawColumns(display, x, y, fields);
}

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(32)
{
    graphics::normalFrames = new FrameCallback[MAX_NUM_NODES + NUM_EXTRA_FRAMES];
#if defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
    dispdev = new SH1106Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_SSD1306)
    dispdev = new SSD1306Wire(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ST7789_CS) || defined(RAK14014) || defined(HX8357_CS)
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
#elif ARCH_PORTDUINO
    if (settingsMap[displayPanel] != no_screen) {
        LOG_DEBUG("Making TFTDisplay!\n");
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
            LOG_INFO("Turning on screen\n");
#ifdef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_ALDO2);
#endif
#if !ARCH_PORTDUINO
            dispdev->displayOn();
#endif

#if defined(ST7789_CS) &&                                                                                                        \
    !defined(M5STACK) // set display brightness when turning on screens. Just moved function from TFTDisplay to here.
            static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

            dispdev->displayOn();

            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
#ifdef USE_EINK
            // eInkScreensaver parameter is usually NULL (default argument), default frame used instead
            setScreensaverFrames(einkScreensaver);
#endif
            LOG_INFO("Turning off screen\n");
            dispdev->displayOff();
#ifdef T_WATCH_S3
            PMU->disablePowerOutput(XPOWERS_ALDO2);
#endif
            enabled = false;
        }
        screenOn = on;
    }
}

void Screen::setup()
{
    // We don't set useDisplay until setup() is called, because some boards have a declaration of this object but the device
    // is never found when probing i2c and therefore we don't call setup and never want to do (invalid) accesses to this device.
    useDisplay = true;

#ifdef AutoOLEDWire_h
    if (isAUTOOled)
        static_cast<AutoOLEDWire *>(dispdev)->setDetected(model);
#endif

#ifdef USE_SH1107_128_64
    static_cast<SH1106Wire *>(dispdev)->setSubtype(7);
#endif

    // Initialising the UI will init the display too.
    ui->init();

    displayWidth = dispdev->width();
    displayHeight = dispdev->height();

    ui->setTimePerTransition(0);

    ui->setIndicatorPosition(BOTTOM);
    // Defines where the first frame is located in the bar.
    ui->setIndicatorDirection(LEFT_RIGHT);
    ui->setFrameAnimation(SLIDE_LEFT);
    // Don't show the page swipe dots while in boot screen.
    ui->disableAllIndicators();
    // Store a pointer to Screen so we can get to it from static functions.
    ui->getUiState()->userData = this;

    // Set the utf8 conversion function
    dispdev->setFontTableLookupFunction(customFontTableLookup);

    if (strlen(oemStore.oem_text) > 0)
        logo_timeout *= 2;

    // Add frames.
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST);
    static FrameCallback bootFrames[] = {drawBootScreen};
    static const int bootFrameCount = sizeof(bootFrames) / sizeof(bootFrames[0]);
    ui->setFrames(bootFrames, bootFrameCount);
    // No overlays.
    ui->setOverlays(nullptr, 0);

    // Require presses to switch between frames.
    ui->disableAutoTransition();

    // Set up a log buffer with 3 lines, 32 chars each.
    dispdev->setLogBuffer(3, 32);

#ifdef SCREEN_MIRROR
    dispdev->mirrorScreen();
#else
    // Standard behaviour is to FLIP the screen (needed on T-Beam). If this config item is set, unflip it, and thereby logically
    // flip it. If you have a headache now, you're welcome.
    if (!config.display.flip_screen) {
#if defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ST7789_CS) || defined(RAK14014) || defined(HX8357_CS)
        static_cast<TFTDisplay *>(dispdev)->flipScreenVertically();
#else
        dispdev->flipScreenVertically();
#endif
    }
#endif

    // Get our hardware ID
    uint8_t dmac[6];
    getMacAddr(dmac);
    snprintf(ourId, sizeof(ourId), "%02x%02x", dmac[4], dmac[5]);
#if ARCH_PORTDUINO
    handleSetOn(false); // force clean init
#endif

    // Turn on the display.
    handleSetOn(true);

    // On some ssd1306 clones, the first draw command is discarded, so draw it
    // twice initially. Skip this for EINK Displays to save a few seconds during boot
    ui->update();
#ifndef USE_EINK
    ui->update();
#endif
    serialSinceMsec = millis();

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

    // Subscribe to status updates
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);
    if (inputBroker)
        inputObserver.observe(inputBroker);
    if (waypointModule)
        waypointObserver.observe(waypointModule);

    // Modules can notify screen about refresh
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay(bool forceUiUpdate)
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    // If requested, make sure queued commands are run, and UI has rendered a new frame
    if (forceUiUpdate) {
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
        LOG_INFO("Done with boot screen...\n");
        stopBootScreen();
        showingBootScreen = false;
    }

    // If we have an OEM Boot screen, toggle after logo_timeout seconds
    if (strlen(oemStore.oem_text) > 0) {
        static bool showingOEMBootScreen = true;
        if (showingOEMBootScreen && (millis() > ((logo_timeout / 2) + serialSinceMsec))) {
            LOG_INFO("Switch to OEM screen...\n");
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
    }

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
            // If a nag notification is running, stop it
            if (moduleConfig.external_notification.enabled && (externalNotificationModule->nagCycleCutoff != UINT32_MAX)) {
                externalNotificationModule->stopNow();
            } else {
                // Don't advance the screen if we just wanted to switch off the nag notification
                handleOnPress();
            }
            break;
        case Cmd::SHOW_PREV_FRAME:
            handleShowPrevFrame();
            break;
        case Cmd::SHOW_NEXT_FRAME:
            handleShowNextFrame();
            break;
        case Cmd::START_BLUETOOTH_PIN_SCREEN:
            handleStartBluetoothPinScreen(cmd.bluetooth_pin);
            break;
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_BLUETOOTH_PIN_SCREEN:
        case Cmd::STOP_BOOT_SCREEN:
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // E-Ink: Explicitly use full-refresh for next frame
            setFrames();
            break;
        case Cmd::PRINT:
            handlePrint(cmd.print_text);
            free(cmd.print_text);
            break;
        case Cmd::START_SHUTDOWN_SCREEN:
            handleShutdownScreen();
            break;
        case Cmd::START_REBOOT_SCREEN:
            handleRebootScreen();
            break;
        default:
            LOG_ERROR("Invalid screen cmd\n");
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
            (millis() - lastScreenTransition) > (config.display.auto_screen_carousel_secs * 1000)) {
            LOG_DEBUG("LastScreenTransition exceeded %ums transitioning to next frame\n", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // LOG_DEBUG("want fps %d, fixed=%d\n", targetFramerate,
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
        // LOG_DEBUG("showing SSL frames\n");
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
        // LOG_DEBUG("showing Welcome frames\n");
        static FrameCallback frames[] = {drawWelcomeScreen};
        setFrameImmediateDraw(frames);
    }
}

#ifdef USE_EINK
/// Determine which screensaver frame to use, then set the FrameCallback
void Screen::setScreensaverFrames(FrameCallback einkScreensaver)
{
    // Remember current frame, restore position at power-on
    uint8_t frameNumber = ui->getUiState()->currentFrame;

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
    ui->setOverlays(NULL, 0);       // Clear overlay
    setFrames();                    // Return to normal display updates
    ui->switchToFrame(frameNumber); // Attempt to return to same frame after power-on

    // Pick a refresh method, for when display wakes
#ifdef EINK_HASQUIRK_GHOSTING
    EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // Really ugly to see ghosting from "screen paused"
#else
    EINK_ADD_FRAMEFLAG(dispdev, RESPONSIVE); // Really nice to wake screen with a fast-refresh
#endif
}
#endif

// restore our regular frame list
void Screen::setFrames()
{
    LOG_DEBUG("showing standard frames\n");
    showingNormalScreen = true;

#ifdef USE_EINK
    // If user has disabled the screensaver, warn them after boot
    static bool warnedScreensaverDisabled = false;
    if (config.display.screen_on_secs == 0 && !warnedScreensaverDisabled) {
        screen->print("Screensaver disabled\n");
        warnedScreensaverDisabled = true;
    }
#endif

    moduleFrames = MeshModule::GetMeshModulesWithUIFrames();
    LOG_DEBUG("Showing %d module frames\n", moduleFrames.size());
#ifdef DEBUG_PORT
    int totalFrameCount = MAX_NUM_NODES + NUM_EXTRA_FRAMES + moduleFrames.size();
    LOG_DEBUG("Total frame count: %d\n", totalFrameCount);
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
        normalFrames[numframes++] = drawModuleFrame;
    }

    LOG_DEBUG("Added modules.  numframes: %d\n", numframes);

    // If we have a critical fault, show it first
    if (error_code)
        normalFrames[numframes++] = drawCriticalFaultFrame;

#ifdef T_WATCH_S3
    normalFrames[numframes++] = screen->digitalWatchFace ? &Screen::drawDigitalClockFrame : &Screen::drawAnalogClockFrame;
#endif

    // If we have a text message - show it next, unless it's a phone message and we aren't using any special modules
    if (devicestate.has_rx_text_message && shouldDrawMessage(&devicestate.rx_text_message)) {
        normalFrames[numframes++] = drawTextMessageFrame;
    }

    // If we have a waypoint (not expired, not deleted)
    if (devicestate.has_rx_waypoint && shouldDrawWaypoint(&devicestate.rx_waypoint)) {
        normalFrames[numframes++] = drawWaypointFrame;
    }

    // then all the nodes
    // We only show a few nodes in our scrolling list - because meshes with many nodes would have too many screens
    size_t numToShow = min(numMeshNodes, 4U);
    for (size_t i = 0; i < numToShow; i++)
        normalFrames[numframes++] = drawNodeInfo;

    // then the debug info
    //
    // Since frames are basic function pointers, we have to use a helper to
    // call a method on debugInfo object.
    normalFrames[numframes++] = &Screen::drawDebugInfoTrampoline;

    // call a method on debugInfoScreen object (for more details)
    normalFrames[numframes++] = &Screen::drawDebugInfoSettingsTrampoline;

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (isWifiAvailable()) {
        // call a method on debugInfoScreen object (for more details)
        normalFrames[numframes++] = &Screen::drawDebugInfoWiFiTrampoline;
    }
#endif

    LOG_DEBUG("Finished building frames. numframes: %d\n", numframes);

    ui->setFrames(normalFrames, numframes);
    ui->enableAllIndicators();

    // Add function overlay here. This can show when notifications muted, modifier key is active etc
    static OverlayCallback functionOverlay[] = {drawFunctionOverlay};
    static const int functionOverlayCount = sizeof(functionOverlay) / sizeof(functionOverlay[0]);
    ui->setOverlays(functionOverlay, functionOverlayCount);

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list
                    // just changed)

    setFastFramerate(); // Draw ASAP
}

void Screen::handleStartBluetoothPinScreen(uint32_t pin)
{
    LOG_DEBUG("showing bluetooth screen\n");
    showingNormalScreen = false;
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // E-Ink: Explicitly use fast-refresh for next frame

    static FrameCallback frames[] = {drawFrameBluetooth};
    snprintf(btPIN, sizeof(btPIN), "%06u", pin);
    setFrameImmediateDraw(frames);
}

void Screen::setFrameImmediateDraw(FrameCallback *drawFrames)
{
    ui->disableAllIndicators();
    ui->setFrames(drawFrames, 1);
    setFastFramerate();
}

void Screen::handleShutdownScreen()
{
    LOG_DEBUG("showing shutdown screen\n");
    showingNormalScreen = false;
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
    EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
    handleSetOn(true);                        // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
#endif

    auto frame = [](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
        drawFrameText(display, state, x, y, "Shutting down...");
    };
    static FrameCallback frames[] = {frame};

    setFrameImmediateDraw(frames);
}

void Screen::handleRebootScreen()
{
    LOG_DEBUG("showing reboot screen\n");
    showingNormalScreen = false;
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
    EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
    handleSetOn(true);                        // Power-on to show rebooting screen (PowerFSM should handle?)
#endif

    auto frame = [](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
        drawFrameText(display, state, x, y, "Rebooting...");
    };
    static FrameCallback frames[] = {frame};
    setFrameImmediateDraw(frames);
}

void Screen::handleStartFirmwareUpdateScreen()
{
    LOG_DEBUG("showing firmware screen\n");
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
        dispdev->fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
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

void Screen::setFunctionSymbal(std::string sym)
{
    if (std::find(functionSymbals.begin(), functionSymbals.end(), sym) == functionSymbals.end()) {
        functionSymbals.push_back(sym);
        functionSymbalString = "";
        for (auto symbol : functionSymbals) {
            functionSymbalString = symbol + " " + functionSymbalString;
        }
        setFastFramerate();
    }
}

void Screen::removeFunctionSymbal(std::string sym)
{
    functionSymbals.erase(std::remove(functionSymbals.begin(), functionSymbals.end(), sym), functionSymbals.end());
    functionSymbalString = "";
    for (auto symbol : functionSymbals) {
        functionSymbalString = symbol + " " + functionSymbalString;
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
    LOG_DEBUG("Screen: %.*s\n", strlen(text) - 1, text);
    if (!useDisplay || !showingNormalScreen)
        return;

    dispdev->print(text);
}

void Screen::handleOnPress()
{
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

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
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
        if (millis() - storeForwardModule->lastHeartbeat >
            (storeForwardModule->heartbeatInterval * 1200)) { // no heartbeat, overlap a bit
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7735_CS) || defined(ST7789_CS) || defined(HX8357_CS)) &&          \
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
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7735_CS) || defined(ST7789_CS) || defined(HX8357_CS)) &&          \
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
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7735_CS) || defined(ST7789_CS) || defined(HX8357_CS) ||           \
     ARCH_PORTDUINO) &&                                                                                                          \
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

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
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

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
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

    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, true);

    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode), y, mode);
    if (config.display.heading_bold)
        display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode) - 1, y, mode);

    // Line 2
    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    // currentMillis %= 1000;
    // seconds %= 60;
    // minutes %= 60;
    // hours %= 24;

    display->setColor(WHITE);

    // Show uptime as days, hours, minutes OR seconds
    std::string uptime = screen->drawTimeDelta(days, hours, minutes, seconds);

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

        char timebuf[10];
        snprintf(timebuf, sizeof(timebuf), " %02d:%02d:%02d", hour, min, sec);
        uptime += timebuf;
    }

    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, uptime.c_str());

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
    // LOG_DEBUG("Screen got status update %d\n", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(); // Regen the list of screens
        }
        nodeDB->updateGUI = false;
        break;
    }

    return 0;
}

int Screen::handleTextMessage(const meshtastic_MeshPacket *packet)
{
    if (showingNormalScreen) {
        setFrames(); // Regen the list of screens (will show new text message)
    }

    return 0;
}

int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    if (showingNormalScreen) {
        if (event->frameChanged) {
            setFrames(); // Regen the list of screens (will show new text message)
        } else if (event->needRedraw) {
            setFastFramerate();
            // TODO: We might also want switch to corresponding frame,
            //       but we don't know the exact frame number.
            // ui->switchToFrame(0);
        }
    }

    return 0;
}

int Screen::handleInputEvent(const InputEvent *event)
{

#ifdef T_WATCH_S3
    // For the T-Watch, intercept touches to the 'toggle digital/analog watch face' button
    uint8_t watchFaceFrame = error_code ? 1 : 0;

    if (this->ui->getUiState()->currentFrame == watchFaceFrame && event->touchX >= 204 && event->touchX <= 240 &&
        event->touchY >= 204 && event->touchY <= 240) {
        screen->digitalWatchFace = !screen->digitalWatchFace;

        setFrames();

        return 0;
    }
#endif

    if (showingNormalScreen && moduleFrames.size() == 0) {
        // LOG_DEBUG("Screen::handleInputEvent from %s\n", event->source);
        if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT)) {
            showPrevFrame();
        } else if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT)) {
            showNextFrame();
        }
    }

    return 0;
}

int Screen::handleWaypoint(const meshtastic_MeshPacket *arg)
{
    // TODO: move to appropriate frame when redrawing
    setFrames();
    return 0;
}

} // namespace graphics
#else
graphics::Screen::Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY) {}
#endif // HAS_SCREEN