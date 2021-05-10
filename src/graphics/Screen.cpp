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

#include <OLEDDisplay.h>

#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Screen.h"
#include "configuration.h"
#include "fonts.h"
#include "gps/RTC.h"
#include "graphics/images.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "plugins/TextMessagePlugin.h"
#include "target_specific.h"
#include "utils.h"

#ifndef NO_ESP32
#include "mesh/http/WiFiAPClient.h"
#endif

using namespace meshtastic; /** @todo remove */

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

// At some point, we're going to ask all of the plugins if they would like to display a screen frame
// we'll need to hold onto pointers for the plugins that can draw a frame.
std::vector<MeshPlugin *> pluginFrames;

// Stores the last 4 of our hardware ID, to make finding the device for pairing easier
static char ourId[5];

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

static uint16_t displayWidth, displayHeight;

#define SCREEN_WIDTH displayWidth
#define SCREEN_HEIGHT displayHeight

#ifdef HAS_EINK
// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16
#define FONT_MEDIUM ArialMT_Plain_24
#define FONT_LARGE ArialMT_Plain_24
#else
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24
#endif

#define fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM fontHeight(FONT_MEDIUM)

#define getStringCenteredX(s) ((SCREEN_WIDTH - display->getStringWidth(s)) / 2)

#ifndef SCREEN_TRANSITION_MSECS
#define SCREEN_TRANSITION_MSECS 300
#endif

/**
 * Draw the icon with extra info printed around the corners
 */
static void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
    display->drawXbm(x + (SCREEN_WIDTH - icon_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 7,
                     icon_width, icon_height, (const uint8_t *)icon_bits);

    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = "ACT Wildlife";
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version in upper right
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION)); // Note: we don't bother printing region or now, it makes the string too long
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

#ifdef HAS_EINK
/// Used on eink displays while in deep sleep
static void drawSleepScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawIconScreen("Sleeping...", display, state, x, y);
}
#endif

static void drawPluginFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t plugin_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        plugin_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the plugin frame that's aligned with the current frame
        plugin_frame = state->currentFrame;
        // DEBUG_MSG("Screen is not in transition.  Frame: %d\n\n", plugin_frame);
    }
    // DEBUG_MSG("Drawing Plugin Frame %d\n\n", plugin_frame);
    MeshPlugin &pi = *pluginFrames.at(plugin_frame);
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
    display->drawString(64 + x, 26 + y, btPIN);

    display->setFont(FONT_SMALL);
    char buf[30];
    const char *name = "Name: ";
    strcpy(buf, name);
    strcat(buf, getDeviceName());
    display->drawString(64 + x, 48 + y, buf);
}

static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Updating");

    display->setFont(FONT_SMALL);
    display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait...");

    // display->setFont(FONT_LARGE);
    // display->drawString(64 + x, 26 + y, btPIN);
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
    display->drawString(0 + x, FONT_HEIGHT_MEDIUM + y, "For help, please post on\nmeshtastic.discourse.group");
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
    if (!gps->getIsConnected()) {
        // displayLine = "No GPS Module";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock()) {
        // displayLine = "No GPS Lock";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {

        displayLine = "Altitude: " + String(gps->getAltitude()) + "m";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}

// Draw GPS status coordinates
static void drawGPScoordinates(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    String displayLine = "";
    if (!gps->getIsConnected()) {
        displayLine = "No GPS Module";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock()) {
        displayLine = "No GPS Lock";
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {
        char coordinateLine[22];
        sprintf(coordinateLine, "%f %f", gps->getLatitude() * 1e-7, gps->getLongitude() * 1e-7);
        display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(coordinateLine))) / 2, y, coordinateLine);
    }
}

/// Ported from my old java code, returns distance in meters along the globe
/// surface (by magic?)
static float latLongToMeter(double lat_a, double lng_a, double lat_b, double lng_b)
{
    double pk = (180 / 3.14169);
    double a1 = lat_a / pk;
    double a2 = lng_a / pk;
    double b1 = lat_b / pk;
    double b2 = lng_b / pk;
    double cos_b1 = cos(b1);
    double cos_a1 = cos(a1);
    double t1 = cos_a1 * cos(a2) * cos_b1 * cos(b2);
    double t2 = cos_a1 * sin(a2) * cos_b1 * sin(b2);
    double t3 = sin(a1) * sin(b1);
    double tt = acos(t1 + t2 + t3);
    if (isnan(tt))
        tt = 0.0; // Must have been the same point?

    return (float)(6366000 * tt);
}

static inline double toRadians(double deg)
{
    return deg * PI / 180;
}

static inline double toDegrees(double r)
{
    return r * 180 / PI;
}

/**
 * Computes the bearing in degrees between two points on Earth.  Ported from my
 * old Gaggle android app.
 *
 * @param lat1
 * Latitude of the first point
 * @param lon1
 * Longitude of the first point
 * @param lat2
 * Latitude of the second point
 * @param lon2
 * Longitude of the second point
 * @return Bearing between the two points in radians. A value of 0 means due
 * north.
 */
static float bearing(double lat1, double lon1, double lat2, double lon2)
{
    double lat1Rad = toRadians(lat1);
    double lat2Rad = toRadians(lat2);
    double deltaLonRad = toRadians(lon2 - lon1);
    double y = sin(deltaLonRad) * cos(lat2Rad);
    double x = cos(lat1Rad) * sin(lat2Rad) - (sin(lat1Rad) * cos(lat2Rad) * cos(deltaLonRad));
    return atan2(y, x);
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
        float rx = x * cos - y * sin, ry = x * sin + y * cos;

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
        x *= f;
        y *= f;
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

    float d = latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = bearing(oldLat, oldLon, lat, lon);
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

// Draw the compass heading
static void drawCompassHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading)
{
    Point N1(-0.04f, -0.65f), N2(0.04f, -0.65f);
    Point N3(-0.04f, -0.55f), N4(0.04f, -0.55f);
    Point *rosePoints[] = {&N1, &N2, &N3, &N4};

    for (int i = 0; i < 4; i++) {
        rosePoints[i]->rotate(myHeading);
        rosePoints[i]->scale(-1 * COMPASS_DIAM);
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

        // We just changed to a new node screen, ask that node for updated state if it's older than 2 minutes
        if (sinceLastSeen(n) > 120) {
            service.sendNetworkPing(displayedNodeNum, true);
        }
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
    else
        snprintf(lastStr, sizeof(lastStr), "%u hours ago", agoSecs / 60 / 60);

    static char distStr[20];
    strcpy(distStr, "? km"); // might not have location data
    float headingRadian;
    NodeInfo *ourNode = nodeDB.getNode(nodeDB.getNodeNum());
    const char *fields[] = {username, distStr, signalStr, lastStr, NULL};

    // coordinates for the center of the compass/circle
    int16_t compassX = x + SCREEN_WIDTH - COMPASS_DIAM / 2 - 5, compassY = y + SCREEN_HEIGHT / 2;
    bool hasNodeHeading = false;

    if (ourNode && hasPosition(ourNode)) {
        Position &op = ourNode->position;
        float myHeading = estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        drawCompassHeading(display, compassX, compassY, myHeading);

        if (hasPosition(node)) {
            // display direction toward node
            hasNodeHeading = true;
            Position &p = node->position;
            float d = latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            if (d < 2000)
                snprintf(distStr, sizeof(distStr), "%.0f m", d);
            else
                snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);

            // FIXME, also keep the guess at the operators heading and add/substract
            // it.  currently we don't do this and instead draw north up only.
            float bearingToOther = bearing(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            headingRadian = bearingToOther - myHeading;
            drawNodeHeading(display, compassX, compassY, headingRadian);
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

Screen::Screen(uint8_t address, int sda, int scl) : OSThread("Screen"), cmdQueue(32), dispdev(address, sda, scl), ui(&dispdev)
{
    cmdQueue.setReader(this);
}

/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef HAS_EINK
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

    // I think this is not needed - redundant with ui.init
    // dispdev.resetOrientation();

    // Initialising the UI will init the display too.
    ui.init();

    displayWidth = dispdev.width();
    displayHeight = dispdev.height();

    ui.setTimePerTransition(SCREEN_TRANSITION_MSECS);

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
    // twice initially.
    ui.update();
    ui.update();

    // Subscribe to status updates
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);
    if (textMessagePlugin)
        textMessageObserver.observe(textMessagePlugin);
}

void Screen::forceDisplay()
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef HAS_EINK
    dispdev.forceDisplay();
#endif
}

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    // Show boot screen for first 3 seconds, then switch to normal operation.
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > 5000)) {
        DEBUG_MSG("Done with boot screen...\n");
        stopBootScreen();
        showingBootScreen = false;
    }

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
    Screen *screen = reinterpret_cast<Screen *>(state->userData);
    screen->debugInfo.drawFrame(display, state, x, y);
}

void Screen::drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen = reinterpret_cast<Screen *>(state->userData);
    screen->debugInfo.drawFrameSettings(display, state, x, y);
}

void Screen::drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen = reinterpret_cast<Screen *>(state->userData);
    screen->debugInfo.drawFrameWiFi(display, state, x, y);
}

// restore our regular frame list
void Screen::setFrames()
{
    DEBUG_MSG("showing standard frames\n");
    showingNormalScreen = true;

    pluginFrames = MeshPlugin::GetMeshPluginsWithUIFrames();
    DEBUG_MSG("Showing %d plugin frames\n", pluginFrames.size());
    int totalFrameCount = MAX_NUM_NODES + NUM_EXTRA_FRAMES + pluginFrames.size();
    DEBUG_MSG("Total frame count: %d\n", totalFrameCount);

    // We don't show the node info our our node (if we have it yet - we should)
    size_t numnodes = nodeStatus->getNumTotal();
    if (numnodes > 0)
        numnodes--;

    size_t numframes = 0;

    // put all of the plugin frames first.
    // this is a little bit of a dirty hack; since we're going to call
    // the same drawPluginFrame handler here for all of these plugin frames
    // and then we'll just assume that the state->currentFrame value
    // is the same offset into the pluginFrames vector
    // so that we can invoke the plugin's callback
    for (auto i = pluginFrames.begin(); i != pluginFrames.end(); ++i) {
        normalFrames[numframes++] = drawPluginFrame;
    }

    DEBUG_MSG("Added plugins.  numframes: %d\n", numframes);

    // If we have a critical fault, show it first
    if (myNodeInfo.error_code)
        normalFrames[numframes++] = drawCriticalFaultFrame;

    // If we have a text message - show it next
    if (devicestate.has_rx_text_message)
        normalFrames[numframes++] = drawTextMessageFrame;

    // then all the nodes
    for (size_t i = 0; i < numnodes; i++)
        normalFrames[numframes++] = drawNodeInfo;

    // then the debug info
    //
    // Since frames are basic function pointers, we have to use a helper to
    // call a method on debugInfo object.
    normalFrames[numframes++] = &Screen::drawDebugInfoTrampoline;

    // call a method on debugInfoScreen object (for more details)
    normalFrames[numframes++] = &Screen::drawDebugInfoSettingsTrampoline;

#ifndef NO_ESP32
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

    snprintf(btPIN, sizeof(btPIN), "%06lu", pin);

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
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
#ifdef HAS_WIFI
    const char *wifiName = radioConfig.preferences.wifi_ssid;
    const char *wifiPsw = radioConfig.preferences.wifi_password;

    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (isSoftAPForced()) {
        display->drawString(x, y, String("WiFi: Software AP (Admin)"));
    } else if (radioConfig.preferences.wifi_ap_mode) {
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
    if (WiFi.status() == WL_CONNECTED || isSoftAPForced() || radioConfig.preferences.wifi_ap_mode) {
        if (radioConfig.preferences.wifi_ap_mode || isSoftAPForced()) {
            display->drawString(x, y + FONT_HEIGHT_SMALL * 1, "IP: " + String(WiFi.softAPIP().toString().c_str()));

            // Number of connections to the AP. Default mmax for the esp32 is 4
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
        if (radioConfig.preferences.wifi_ap_mode) {
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

    auto mode = "Mode " + String(channels.getPrimary().modem_config);
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

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityFromNet);
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

#ifndef NO_ESP32
    // Show CPU Frequency.
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth("CPU " + String(getCpuFrequencyMhz()) + "MHz"),
                        y + FONT_HEIGHT_SMALL * 1, "CPU " + String(getCpuFrequencyMhz()) + "MHz");
#endif

    // Line 3
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

int Screen::handleTextMessage(const MeshPacket *arg)
{
    if (showingNormalScreen) {
        setFrames(); // Regen the list of screens (will show new text message)
    }

    return 0;
}

} // namespace graphics
