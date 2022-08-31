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
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>

#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Screen.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/images.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/deviceonly.pb.h"
#include "modules/TextMessageModule.h"
#include "sleep.h"
#include "target_specific.h"
#include "utils.h"

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#include "mesh/http/WiFiAPClient.h"
#endif

#ifdef OLED_RU
#include "fonts/OLEDDisplayFontsRU.h"
#endif

using namespace meshtastic; /** @todo remove */

extern bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);

namespace graphics
{

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps
#define COMPASS_DIAM 44

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS

// A text message frame + debug frame + all the node infos
static FrameCallback normalFrames[MAX_NUM_NODES + NUM_EXTRA_FRAMES];
static uint32_t targetFramerate = IDLE_FRAMERATE;
static char btPIN[16] = "888888";
    
// This image definition is here instead of images.h because it's modified dynamically by the drawBattery function
uint8_t imgBattery[16] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xE7, 0x3C};

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Stores the last 4 of our hardware ID, to make finding the device for pairing easier
static char ourId[5];

// GeoCoord object for the screen
GeoCoord geoCoord;

// OEM Config File
static const char *oemConfigFile = "/prefs/oem.proto";
OEMStore oemStore;

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

static uint16_t displayWidth, displayHeight;

#define SCREEN_WIDTH displayWidth
#define SCREEN_HEIGHT displayHeight

#if defined(USE_EINK) || defined(ILI9341_DRIVER)
// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16
#define FONT_MEDIUM ArialMT_Plain_24
#define FONT_LARGE ArialMT_Plain_24
#else
#ifdef OLED_RU
#define FONT_SMALL ArialMT_Plain_10_RU
#else
#define FONT_SMALL ArialMT_Plain_10
#endif
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24
#endif

#define fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM fontHeight(FONT_MEDIUM)

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
                     icon_width, icon_height, (const uint8_t *)icon_bits);

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

static void drawBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawIconScreen(region, display, state, x, y);
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
        display->drawString(x, y + FONT_HEIGHT_SMALL * 4 - 3, "Flasher or CLI client.");
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

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
static void drawSleepScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawIconScreen("Sleeping...", display, state, x, y);
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
        // DEBUG_MSG("Screen is not in transition.  Frame: %d\n\n", module_frame);
    }
    // DEBUG_MSG("Drawing Module Frame %d\n\n", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

static void drawFrameBluetooth(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Bluetooth");

    display->setFont(FONT_SMALL);
    display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Enter this code");

    display->setFont(FONT_LARGE);

    auto displayPin = new String(btPIN);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(12 + x, 26 + y, displayPin->substring(0, 3));
    display->drawString(72 + x, 26 + y, displayPin->substring(3, 6));
    
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    char buf[30];
    const char *name = "Name: ";
    strcpy(buf, name);
    strcat(buf, getDeviceName());
    display->drawString(64 + x, 48 + y, buf);
}

static void drawFrameShutdown(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, 26 + y, "Shutting down...");
}

static void drawFrameReboot(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, 26 + y, "Rebooting...");
}

static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Updating");

    display->setFont(FONT_SMALL);
    if ((millis() / 1000) % 2) {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . . .");
    } else {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . .  ");
    }
}

/// Draw the last text message we received
static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);

    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "Critical fault #%d", myNodeInfo.error_code);
    display->drawString(0 + x, 0 + y, tempBuf);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(0 + x, FONT_HEIGHT_MEDIUM + y, "For help, please visit \nmeshtastic.org");
}

// Ignore messages orginating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.range_test.enabled &&
           !moduleConfig.store_forward.enabled;
}

/// Draw the last text message we received
static void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    MeshPacket &mp = devicestate.rx_text_message;
    NodeInfo *node = nodeDB.getNode(getFrom(&mp));
    // DEBUG_MSG("drawing text message from 0x%x: %s\n", mp.from,
    // mp.decoded.variant.data.decoded.bytes);

    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will
    // be wrapped. Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    String sender = (node && node->has_user) ? node->user.short_name : "???";
    display->drawString(0 + x, 0 + y, sender);
    display->setFont(FONT_SMALL);

    // the max length of this buffer is much longer than we can possibly print
    static char tempBuf[96];
    snprintf(tempBuf, sizeof(tempBuf), "         %s", mp.decoded.payload.bytes);

    display->drawStringMaxWidth(4 + x, 10 + y, SCREEN_WIDTH - (6 + x), tempBuf);
}

/// Draw a series of fields in a column, wrapping to multiple colums if needed
static void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
{
    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char **f = fields;
    int xo = x, yo = y;
    while (*f) {
        display->drawString(xo, yo, *f);
        yo += FONT_HEIGHT_SMALL;
        if (yo > SCREEN_HEIGHT - FONT_HEIGHT_SMALL) {
            xo += SCREEN_WIDTH / 2;
            yo = 0;
        }
        f++;
    }
}

#if 0
    /// Draw a series of fields in a row, wrapping to multiple rows if needed
    /// @return the max y we ended up printing to
    static uint32_t drawRows(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
    {
        // The coordinates define the left starting point of the text
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        const char **f = fields;
        int xo = x, yo = y;
        const int COLUMNS = 2; // hardwired for two columns per row....
        int col = 0;           // track which column we are on
        while (*f) {
            display->drawString(xo, yo, *f);
            xo += SCREEN_WIDTH / COLUMNS;
            // Wrap to next row, if needed.
            if (++col >= COLUMNS) {
                xo = x;
                yo += FONT_HEIGHT_SMALL;
                col = 0;
            }
            f++;
        }
        if (col != 0) {
            // Include last incomplete line in our total.
            yo += FONT_HEIGHT_SMALL;
        }

        return yo;
    }
#endif

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

// Draw nodes status
static void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, NodeStatus *nodeStatus)
{
    char usersString[20];
    sprintf(usersString, "%d/%d", nodeStatus->getNumOnline(), nodeStatus->getNumTotal());
    display->drawFastImage(x, y, 8, 8, imgUser);
    display->drawString(x + 10, y - 2, usersString);
}

// Draw GPS status summary
static void drawGPS(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        display->drawString(x - 1, y - 2, "Fixed GPS");
        return;
    }
    if (!gps->getIsConnected()) {
        display->drawString(x, y - 2, "No GPS");
        return;
    }
    display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gps->getHasLock()) {
        display->drawString(x + 8, y - 2, "No sats");
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
        sprintf(satsString, "%u", gps->getNumSatellites());
        display->drawString(x + 34, y - 2, satsString);
    }
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
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}

// Draw GPS status coordinates
static void drawGPScoordinates(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    auto gpsFormat = config.display.gps_format;
    String displayLine = "";

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        displayLine = "No GPS Module";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        displayLine = "No GPS Lock";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {

        if (gpsFormat != Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDMS) {
            char coordinateLine[22];
            geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
            if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDec) { // Decimal Degrees
                sprintf(coordinateLine, "%f %f", geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatUTM) { // Universal Transverse Mercator
                sprintf(coordinateLine, "%2i%1c %06u %07u", geoCoord.getUTMZone(), geoCoord.getUTMBand(),
                        geoCoord.getUTMEasting(), geoCoord.getUTMNorthing());
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatMGRS) { // Military Grid Reference System
                sprintf(coordinateLine, "%2i%1c %1c%1c %05u %05u", geoCoord.getMGRSZone(), geoCoord.getMGRSBand(),
                        geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k(), geoCoord.getMGRSEasting(),
                        geoCoord.getMGRSNorthing());
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatOLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine);
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatOSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') // OSGR is only valid around the UK region
                    sprintf(coordinateLine, "%s", "Out of Boundary");
                else
                    sprintf(coordinateLine, "%1c%1c %05u %05u", geoCoord.getOSGRE100k(), geoCoord.getOSGRN100k(),
                            geoCoord.getOSGREasting(), geoCoord.getOSGRNorthing());
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
            sprintf(latLine, "%2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(), geoCoord.getDMSLatMin(), geoCoord.getDMSLatSec(),
                    geoCoord.getDMSLatCP());
            sprintf(lonLine, "%3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(), geoCoord.getDMSLonMin(), geoCoord.getDMSLonSec(),
                    geoCoord.getDMSLonCP());
            display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(latLine))) / 2, y - FONT_HEIGHT_SMALL * 1, latLine);
            display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(lonLine))) / 2, y, lonLine);
        }
    }
}

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
        //We use -f here to counter the flip that happens
        //on the y axis when drawing and rotating on screen
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

/// Sometimes we will have Position objects that only have a time, so check for
/// valid lat/lon
static bool hasPosition(NodeInfo *n)
{
    return n->has_position && (n->position.latitude_i != 0 || n->position.longitude_i != 0);
}

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
        arrowPoints[i]->scale(COMPASS_DIAM * 0.6);
        arrowPoints[i]->translate(compassX, compassY);
    }
    drawLine(display, tip, tail);
    drawLine(display, leftArrow, tip);
    drawLine(display, rightArrow, tip);
}

// Draw north
static void drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading)
{
    //If north is supposed to be at the top of the compass we want rotation to be +0
    if(config.display.compass_north_top)
        myHeading = -0;
    
    Point N1(-0.04f, 0.65f), N2(0.04f, 0.65f);
    Point N3(-0.04f, 0.55f), N4(0.04f, 0.55f);
    Point *rosePoints[] = {&N1, &N2, &N3, &N4};

    for (int i = 0; i < 4; i++) {
        // North on compass will be negative of heading
        rosePoints[i]->rotate(-myHeading);
        rosePoints[i]->scale(COMPASS_DIAM);
        rosePoints[i]->translate(compassX, compassY);
    }
    drawLine(display, N1, N3);
    drawLine(display, N2, N4);
    drawLine(display, N1, N4);
}

/// Convert an integer GPS coords to a floating point
#define DegD(i) (i * 1e-7)

static void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // We only advance our nodeIndex if the frame # has changed - because
    // drawNodeInfo will be called repeatedly while the frame is shown
    if (state->currentFrame != prevFrame) {
        prevFrame = state->currentFrame;

        nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
        NodeInfo *n = nodeDB.getNodeByIndex(nodeIndex);
        if (n->num == nodeDB.getNodeNum()) {
            // Don't show our node, just skip to next
            nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
            n = nodeDB.getNodeByIndex(nodeIndex);
        }
        displayedNodeNum = n->num;
    }

    NodeInfo *node = nodeDB.getNodeByIndex(nodeIndex);

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char *username = node->has_user ? node->user.long_name : "Unknown Name";

    static char signalStr[20];
    snprintf(signalStr, sizeof(signalStr), "Signal: %d%%", clamp((int)((node->snr + 10) * 5), 0, 100));

    uint32_t agoSecs = sinceLastSeen(node);
    static char lastStr[20];
    if (agoSecs < 120) // last 2 mins?
        snprintf(lastStr, sizeof(lastStr), "%u seconds ago", agoSecs);
    else if (agoSecs < 120 * 60) // last 2 hrs
        snprintf(lastStr, sizeof(lastStr), "%u minutes ago", agoSecs / 60);
    else {

        uint32_t hours_in_month = 730;

        // Only show hours ago if it's been less than 6 months. Otherwise, we may have bad
        //   data.
        if ((agoSecs / 60 / 60) < (hours_in_month * 6)) {
            snprintf(lastStr, sizeof(lastStr), "%u hours ago", agoSecs / 60 / 60);
        } else {
            snprintf(lastStr, sizeof(lastStr), "unknown age");
        }
    }

    static char distStr[20];
    strcpy(distStr, "? km"); // might not have location data
    NodeInfo *ourNode = nodeDB.getNode(nodeDB.getNodeNum());
    const char *fields[] = {username, distStr, signalStr, lastStr, NULL};

    // coordinates for the center of the compass/circle
    int16_t compassX = x + SCREEN_WIDTH - COMPASS_DIAM / 2 - 5, compassY = y + SCREEN_HEIGHT / 2;
    bool hasNodeHeading = false;

    if (ourNode && hasPosition(ourNode)) {
        Position &op = ourNode->position;
        float myHeading = estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        drawCompassNorth(display, compassX, compassY, myHeading);

        if (hasPosition(node)) {
            // display direction toward node
            hasNodeHeading = true;
            Position &p = node->position;
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            if (d < 2000)
                snprintf(distStr, sizeof(distStr), "%.0f m", d);
            else
                snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);

            float bearingToOther =
                GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            // If the top of the compass is a static north then bearingToOther can be drawn on the compass directly
            // If the top of the compass is not a static north we need adjust bearingToOther based on heading
            if(!config.display.compass_north_top)
                bearingToOther -= myHeading;
            drawNodeHeading(display, compassX, compassY, bearingToOther);
        }
    }
    if (!hasNodeHeading)
        // direction to node is unknown so display question mark
        // Debug info for gps lock errors
        // DEBUG_MSG("ourNode %d, ourPos %d, theirPos %d\n", !!ourNode, ourNode && hasPosition(ourNode), hasPosition(node));
        display->drawString(compassX - FONT_HEIGHT_SMALL / 4, compassY - FONT_HEIGHT_SMALL / 2, "?");
    display->drawCircle(compassX, compassY, COMPASS_DIAM / 2);

    // Must be after distStr is populated
    drawColumns(display, x, y, fields);
}

#if 0
void _screen_header()
{
    if (!disp)
        return;

    // Message count
    //snprintf(buffer, sizeof(buffer), "#%03d", ttn_get_count() % 1000);
    //display->setTextAlignment(TEXT_ALIGN_LEFT);
    //display->drawString(0, 2, buffer);

    // Datetime
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth()/2, 2, gps.getTimeStr());

    // Satellite count
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    char buffer[10];
    display->drawString(display->getWidth() - SATELLITE_IMAGE_WIDTH - 4, 2, itoa(gps.satellites.value(), buffer, 10));
    display->drawXbm(display->getWidth() - SATELLITE_IMAGE_WIDTH, 0, SATELLITE_IMAGE_WIDTH, SATELLITE_IMAGE_HEIGHT, SATELLITE_IMAGE);
}
#endif

// #ifdef RAK4630
// Screen::Screen(uint8_t address, int sda, int scl) : OSThread("Screen"), cmdQueue(32), dispdev(address, sda, scl),
// dispdev_oled(address, sda, scl), ui(&dispdev)
// {
//     address_found = address;
//     cmdQueue.setReader(this);
//     if (screen_found) {
//         (void)dispdev;
//         AutoOLEDWire dispdev = dispdev_oled;
//         (void)ui;
//         OLEDDisplayUi ui(&dispdev);
//     }
// }
// #else
Screen::Screen(uint8_t address, int sda, int scl) : OSThread("Screen"), cmdQueue(32), dispdev(address, sda, scl), ui(&dispdev)
{
    address_found = address;
    cmdQueue.setReader(this);
}
// #endif
/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef USE_EINK
    static FrameCallback sleepFrames[] = {drawSleepScreen};
    static const int sleepFrameCount = sizeof(sleepFrames) / sizeof(sleepFrames[0]);
    ui.setFrames(sleepFrames, sleepFrameCount);
    ui.update();
#endif
    setOn(false);
}

void Screen::handleSetOn(bool on)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            DEBUG_MSG("Turning on screen\n");
            dispdev.displayOn();
            dispdev.displayOn();
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            DEBUG_MSG("Turning off screen\n");
            dispdev.displayOff();
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
    dispdev.setDetected(screen_model);
#endif

    // Load OEM config from Proto file if existent
    loadProto(oemConfigFile, OEMStore_size, sizeof(oemConfigFile), OEMStore_fields, &oemStore);

    // Initialising the UI will init the display too.
    ui.init();

    displayWidth = dispdev.width();
    displayHeight = dispdev.height();

    ui.setTimePerTransition(0);

    ui.setIndicatorPosition(BOTTOM);
    // Defines where the first frame is located in the bar.
    ui.setIndicatorDirection(LEFT_RIGHT);
    ui.setFrameAnimation(SLIDE_LEFT);
    // Don't show the page swipe dots while in boot screen.
    ui.disableAllIndicators();
    // Store a pointer to Screen so we can get to it from static functions.
    ui.getUiState()->userData = this;

    // Set the utf8 conversion function
    dispdev.setFontTableLookupFunction(customFontTableLookup);

    // Add frames.
    static FrameCallback bootFrames[] = {drawBootScreen};
    static const int bootFrameCount = sizeof(bootFrames) / sizeof(bootFrames[0]);
    ui.setFrames(bootFrames, bootFrameCount);
    // No overlays.
    ui.setOverlays(nullptr, 0);

    // Require presses to switch between frames.
    ui.disableAutoTransition();

    // Set up a log buffer with 3 lines, 32 chars each.
    dispdev.setLogBuffer(3, 32);

#ifdef SCREEN_MIRROR
    dispdev.mirrorScreen();
#elif defined(SCREEN_FLIP_VERTICALLY)
    dispdev.flipScreenVertically();
#endif

    // Get our hardware ID
    uint8_t dmac[6];
    getMacAddr(dmac);
    sprintf(ourId, "%02x%02x", dmac[4], dmac[5]);

    // Turn on the display.
    handleSetOn(true);

    // On some ssd1306 clones, the first draw command is discarded, so draw it
    // twice initially. Skip this for EINK Displays to save a few seconds during boot
    ui.update();
#ifndef USE_EINK
    ui.update();
#endif
    serialSinceMsec = millis();

    // Subscribe to status updates
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);

    // Modules can notify screen about refresh
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay()
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    dispdev.forceDisplay();
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

    // Show boot screen for first 5 seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (5000 + serialSinceMsec))) {
        DEBUG_MSG("Done with boot screen...\n");
        stopBootScreen();
        showingBootScreen = false;
    }

    // If we have an OEM Boot screen, toggle after 2,5 seconds
    if (strlen(oemStore.oem_text) > 0) {
        static bool showingOEMBootScreen = true;
        if (showingOEMBootScreen && (millis() > (2500 + serialSinceMsec))) {
            DEBUG_MSG("Switch to OEM screen...\n");
            // Change frames.
            static FrameCallback bootOEMFrames[] = {drawOEMBootScreen};
            static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
            ui.setFrames(bootOEMFrames, bootOEMFrameCount);
            ui.update();
            ui.update();
            showingOEMBootScreen = false;
        }
    }

#ifndef DISABLE_WELCOME_UNSET
    if (showingNormalScreen && config.lora.region == Config_LoRaConfig_RegionCode_Unset) {
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
        case Cmd::START_BLUETOOTH_PIN_SCREEN:
            handleStartBluetoothPinScreen(cmd.bluetooth_pin);
            break;
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_BLUETOOTH_PIN_SCREEN:
        case Cmd::STOP_BOOT_SCREEN:
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
            DEBUG_MSG("BUG: invalid cmd\n");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui.update();

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.
    if (targetFramerate != IDLE_FRAMERATE && ui.getUiState()->frameState == FIXED) {
        // oldFrameState = ui.getUiState()->frameState;
        DEBUG_MSG("Setting idle framerate\n");
        targetFramerate = IDLE_FRAMERATE;

        ui.setTargetFPS(targetFramerate);
        forceDisplay();
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            (millis() - lastScreenTransition) > (config.display.auto_screen_carousel_secs * 1000)) {
            DEBUG_MSG("LastScreenTransition exceeded %ums transitioning to next frame\n", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // DEBUG_MSG("want fps %d, fixed=%d\n", targetFramerate,
    // ui.getUiState()->frameState); If we are scrolling we need to be called
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
    if (address_found) {
        // DEBUG_MSG("showing SSL frames\n");
        static FrameCallback sslFrames[] = {drawSSLScreen};
        ui.setFrames(sslFrames, 1);
        ui.update();
    }
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setWelcomeFrames()
{
    if (address_found) {
        // DEBUG_MSG("showing Welcome frames\n");
        ui.disableAllIndicators();

        static FrameCallback welcomeFrames[] = {drawWelcomeScreen};
        ui.setFrames(welcomeFrames, 1);
        ui.update();
    }
}

// restore our regular frame list
void Screen::setFrames()
{
    DEBUG_MSG("showing standard frames\n");
    showingNormalScreen = true;

    moduleFrames = MeshModule::GetMeshModulesWithUIFrames();
    DEBUG_MSG("Showing %d module frames\n", moduleFrames.size());
    int totalFrameCount = MAX_NUM_NODES + NUM_EXTRA_FRAMES + moduleFrames.size();
    DEBUG_MSG("Total frame count: %d\n", totalFrameCount);

    // We don't show the node info our our node (if we have it yet - we should)
    size_t numnodes = nodeStatus->getNumTotal();
    if (numnodes > 0)
        numnodes--;

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

    DEBUG_MSG("Added modules.  numframes: %d\n", numframes);

    // If we have a critical fault, show it first
    if (myNodeInfo.error_code)
        normalFrames[numframes++] = drawCriticalFaultFrame;

    // If we have a text message - show it next, unless it's a phone message and we aren't using any special modules
    if (devicestate.has_rx_text_message && shouldDrawMessage(&devicestate.rx_text_message)) {
        normalFrames[numframes++] = drawTextMessageFrame;
    }

    // then all the nodes
    // We only show a few nodes in our scrolling list - because meshes with many nodes would have too many screens
    size_t numToShow = min(numnodes, 4U);
    for (size_t i = 0; i < numToShow; i++)
        normalFrames[numframes++] = drawNodeInfo;

    // then the debug info
    //
    // Since frames are basic function pointers, we have to use a helper to
    // call a method on debugInfo object.
    normalFrames[numframes++] = &Screen::drawDebugInfoTrampoline;

    // call a method on debugInfoScreen object (for more details)
    normalFrames[numframes++] = &Screen::drawDebugInfoSettingsTrampoline;

#ifdef ARCH_ESP32
    if (isWifiAvailable()) {
        // call a method on debugInfoScreen object (for more details)
        normalFrames[numframes++] = &Screen::drawDebugInfoWiFiTrampoline;
    }
#endif

    DEBUG_MSG("Finished building frames. numframes: %d\n", numframes);

    ui.setFrames(normalFrames, numframes);
    ui.enableAllIndicators();

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list
                    // just changed)

    setFastFramerate(); // Draw ASAP
}

void Screen::handleStartBluetoothPinScreen(uint32_t pin)
{
    DEBUG_MSG("showing bluetooth screen\n");
    showingNormalScreen = false;

    static FrameCallback btFrames[] = {drawFrameBluetooth};

    snprintf(btPIN, sizeof(btPIN), "%06u", pin);

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
    setFastFramerate();
}

void Screen::handleShutdownScreen()
{
    DEBUG_MSG("showing shutdown screen\n");
    showingNormalScreen = false;

    static FrameCallback shutdownFrames[] = {drawFrameShutdown};

    ui.disableAllIndicators();
    ui.setFrames(shutdownFrames, 1);
    setFastFramerate();
}

void Screen::handleRebootScreen()
{
    DEBUG_MSG("showing reboot screen\n");
    showingNormalScreen = false;

    static FrameCallback rebootFrames[] = {drawFrameReboot};

    ui.disableAllIndicators();
    ui.setFrames(rebootFrames, 1);
    setFastFramerate();
}

void Screen::handleStartFirmwareUpdateScreen()
{
    DEBUG_MSG("showing firmware screen\n");
    showingNormalScreen = false;

    static FrameCallback btFrames[] = {drawFrameFirmware};

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
    setFastFramerate();
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev.setBrightness(254);
    while (count > 0) {
        dispdev.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        dispdev.display();
        delay(50);
        dispdev.clear();
        dispdev.display();
        delay(50);
        count = count - 1;
    }
    dispdev.setBrightness(brightness);
}

void Screen::handlePrint(const char *text)
{
    // the string passed into us probably has a newline, but that would confuse the logging system
    // so strip it
    DEBUG_MSG("Screen: %.*s\n", strlen(text) - 1, text);
    if (!useDisplay || !showingNormalScreen)
        return;

    dispdev.print(text);
}

void Screen::handleOnPress()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui.getUiState()->frameState == FIXED) {
        ui.nextFrame();
        DEBUG_MSG("Setting LastScreenTransition\n");
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
    DEBUG_MSG("Setting fast framerate\n");

    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

    ui.setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}

void DebugInfo::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    char channelStr[20];
    {
        concurrency::LockGuard guard(&lock);
        auto chName = channels.getPrimaryName();
        snprintf(channelStr, sizeof(channelStr), "%s", chName);
    }

    // Display power status
    if (powerStatus->getHasBattery())
        drawBattery(display, x, y + 2, imgBattery, powerStatus);
    else if (powerStatus->knowsUSB())
        display->drawFastImage(x, y + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
    // Display nodes status
    drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 2, nodeStatus);
    // Display GPS status
    drawGPS(display, x + (SCREEN_WIDTH * 0.63), y + 2, gpsStatus);

    // Draw the channel name
    display->drawString(x, y + FONT_HEIGHT_SMALL, channelStr);
    // Draw our hardware ID to assist with bluetooth pairing
    display->drawFastImage(x + SCREEN_WIDTH - (10) - display->getStringWidth(ourId), y + 2 + FONT_HEIGHT_SMALL, 8, 8, imgInfo);
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
#if HAS_WIFI
    const char *wifiName = config.wifi.ssid;
    const char *wifiPsw = config.wifi.psk;

    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (isSoftAPForced()) {
        display->drawString(x, y, String("WiFi: Software AP (Admin)"));
    } else if (config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPoint || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPointHidden) {
        display->drawString(x, y, String("WiFi: Software AP"));
    } else if (WiFi.status() != WL_CONNECTED) {
        display->drawString(x, y, String("WiFi: Not Connected"));
    } else {
        display->drawString(x, y, String("WiFi: Connected"));

        display->drawString(x + SCREEN_WIDTH - display->getStringWidth("RSSI " + String(WiFi.RSSI())), y,
                            "RSSI " + String(WiFi.RSSI()));
    }

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
    if (WiFi.status() == WL_CONNECTED || isSoftAPForced() || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPoint || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPointHidden) {
        if (config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPoint || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPointHidden || isSoftAPForced()) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IP: " + String(WiFi.softAPIP().toString().c_str()));

            // Number of connections to the AP. Default max for the esp32 is 4
            display->drawString(x + SCREEN_WIDTH - display->getStringWidth("(" + String(WiFi.softAPgetStationNum()) + "/4)"),
                                y + FONT_HEIGHT_SMALL * 1, "(" + String(WiFi.softAPgetStationNum()) + "/4)");
        } else {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IP: " + String(WiFi.localIP().toString().c_str()));
        }

    } else if (WiFi.status() == WL_NO_SSID_AVAIL) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "SSID Not Found");
    } else if (WiFi.status() == WL_CONNECTION_LOST) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Connection Lost");
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Connection Failed");
        //} else if (WiFi.status() == WL_DISCONNECTED) {
        //    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Disconnected");
    } else if (WiFi.status() == WL_IDLE_STATUS) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Idle ... Reconnecting");
    } else {
        // Codes:
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
        if (getWifiDisconnectReason() == 2) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Authentication Invalid");
        } else if (getWifiDisconnectReason() == 3) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "De-authenticated");
        } else if (getWifiDisconnectReason() == 4) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Disassociated Expired");
        } else if (getWifiDisconnectReason() == 5) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "AP - Too Many Clients");
        } else if (getWifiDisconnectReason() == 6) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "NOT_AUTHED");
        } else if (getWifiDisconnectReason() == 7) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "NOT_ASSOCED");
        } else if (getWifiDisconnectReason() == 8) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Disassociated");
        } else if (getWifiDisconnectReason() == 9) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "ASSOC_NOT_AUTHED");
        } else if (getWifiDisconnectReason() == 10) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "DISASSOC_PWRCAP_BAD");
        } else if (getWifiDisconnectReason() == 11) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "DISASSOC_SUPCHAN_BAD");
        } else if (getWifiDisconnectReason() == 13) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IE_INVALID");
        } else if (getWifiDisconnectReason() == 14) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "MIC_FAILURE");
        } else if (getWifiDisconnectReason() == 15) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "AP Handshake Timeout");
        } else if (getWifiDisconnectReason() == 16) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "GROUP_KEY_UPDATE_TIMEOUT");
        } else if (getWifiDisconnectReason() == 17) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IE_IN_4WAY_DIFFERS");
        } else if (getWifiDisconnectReason() == 18) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Invalid Group Cipher");
        } else if (getWifiDisconnectReason() == 19) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Invalid Pairwise Cipher");
        } else if (getWifiDisconnectReason() == 20) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "AKMP_INVALID");
        } else if (getWifiDisconnectReason() == 21) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "UNSUPP_RSN_IE_VERSION");
        } else if (getWifiDisconnectReason() == 22) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "INVALID_RSN_IE_CAP");
        } else if (getWifiDisconnectReason() == 23) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "802_1X_AUTH_FAILED");
        } else if (getWifiDisconnectReason() == 24) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "CIPHER_SUITE_REJECTED");
        } else if (getWifiDisconnectReason() == 200) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "BEACON_TIMEOUT");
        } else if (getWifiDisconnectReason() == 201) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "AP Not Found");
        } else if (getWifiDisconnectReason() == 202) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "AUTH_FAIL");
        } else if (getWifiDisconnectReason() == 203) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "ASSOC_FAIL");
        } else if (getWifiDisconnectReason() == 204) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "HANDSHAKE_TIMEOUT");
        } else if (getWifiDisconnectReason() == 205) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Connection Failed");
        } else {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "Unknown Status");
        }
    }

    if (isSoftAPForced()) {
        if ((millis() / 10000) % 2) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "SSID: meshtasticAdmin");
        } else {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "PWD: 12345678");
        }

    } else {
        if (config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPoint || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPointHidden) {
            if ((millis() / 10000) % 2) {
                display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "SSID: " + String(wifiName));
            } else {
                display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "PWD: " + String(wifiPsw));
            }
        } else {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 2, "SSID: " + String(wifiName));
        }
    }
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
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "B %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');

        // Line 1
        display->drawString(x, y, batStr);
    } else {
        // Line 1
        display->drawString(x, y, String("USB"));
    }

    auto mode = "";

    switch (config.lora.modem_preset) {
    case Config_LoRaConfig_ModemPreset_ShortSlow:
        mode = "ShortS";
        break;
    case Config_LoRaConfig_ModemPreset_ShortFast:
        mode = "ShortF";
        break;
    case Config_LoRaConfig_ModemPreset_MedSlow:
        mode = "MedS";
        break;
    case Config_LoRaConfig_ModemPreset_MedFast:
        mode = "MedF";
        break;
    case Config_LoRaConfig_ModemPreset_LongSlow:
        mode = "LongS";
        break;
    case Config_LoRaConfig_ModemPreset_LongFast:
        mode = "LongF";
        break;
    case Config_LoRaConfig_ModemPreset_VLongSlow:
        mode = "VeryL";
        break;
    default:
        mode = "Custom";
        break;
    }

    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(mode), y, mode);

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

    // Show uptime as days, hours, minutes OR seconds
    String uptime;
    if (days >= 2)
        uptime += String(days) + "d ";
    else if (hours >= 2)
        uptime += String(hours) + "h ";
    else if (minutes >= 1)
        uptime += String(minutes) + "m ";
    else
        uptime += String(seconds) + "s ";

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice);
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

        char timebuf[9];
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
        uptime += timebuf;
    }

    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, uptime);

    // Display Channel Utilization
    char chUtil[13];
    sprintf(chUtil, "ChUtil %2.0f%%", airTime->channelUtilizationPercent());
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(chUtil), y + FONT_HEIGHT_SMALL * 1, chUtil);

    // Line 3
    if (config.display.gps_format !=
        Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDMS) // if DMS then don't draw altitude
        drawGPSAltitude(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);

    // Line 4
    drawGPScoordinates(display, x, y + FONT_HEIGHT_SMALL * 3, gpsStatus);

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}
// adjust Brightness cycle trough 1 to 254 as long as attachDuringLongPress is true
void Screen::adjustBrightness()
{
    if (brightness == 254) {
        brightness = 0;
    } else {
        brightness++;
    }
    int width = brightness / (254.00 / SCREEN_WIDTH);
    dispdev.drawRect(0, 30, SCREEN_WIDTH, 4);
    dispdev.fillRect(0, 31, width, 2);
    dispdev.display();
    dispdev.setBrightness(brightness);
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // DEBUG_MSG("Screen got status update %d\n", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(); // Regen the list of screens
        }
        nodeDB.updateGUI = false;
        break;
    }

    return 0;
}

int Screen::handleTextMessage(const MeshPacket *packet)
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
            // ui.switchToFrame(0);
        }
    }

    return 0;
}

} // namespace graphics

#endif // HAS_SCREEN
