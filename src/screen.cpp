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
#include "configuration.h"
#include "fonts.h"
#include "images.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "screen.h"
#include "utils.h"

#define FONT_HEIGHT 14 // actually 13 for "ariel 10" but want a little extra space
#define FONT_HEIGHT_16 (ArialMT_Plain_16[1] + 1)
#ifdef USE_SH1106
#define SCREEN_WIDTH 132
#else
#define SCREEN_WIDTH 128
#endif
#define SCREEN_HEIGHT 64
#define TRANSITION_FRAMERATE 30 // fps
#define IDLE_FRAMERATE 1        // in fps
#define COMPASS_DIAM 44

#define NUM_EXTRA_FRAMES 2 // text message and debug frame

namespace meshtastic
{

// A text message frame + debug frame + all the node infos
static FrameCallback normalFrames[MAX_NUM_NODES + NUM_EXTRA_FRAMES];
static uint32_t targetFramerate = IDLE_FRAMERATE;
static char btPIN[16] = "888888";

uint8_t imgBattery[16] = { 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xE7, 0x3C };
static bool heartbeat = false;

static void drawBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y
    display->drawXbm(x + 32, y, icon_width, icon_height, (const uint8_t *)icon_bits);

    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, SCREEN_HEIGHT - FONT_HEIGHT_16, "meshtastic.org");
    display->setFont(ArialMT_Plain_10);
    const char *region = xstr(HW_VERSION);
    if (*region && region[3] == '-') // Skip past 1.0- in the 1.0-EU865 string
        region += 4;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION)); // Note: we don't bother printing region or now, it makes the string too long
    display->drawString(SCREEN_WIDTH - 20, 0, buf);
}

static void drawFrameBluetooth(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_16);
    display->drawString(64 + x, y, "Bluetooth");

    display->setFont(ArialMT_Plain_10);
    display->drawString(64 + x, FONT_HEIGHT + y + 2, "Enter this code");

    display->setFont(ArialMT_Plain_24);
    display->drawString(64 + x, 26 + y, btPIN);

    display->setFont(ArialMT_Plain_10);
    char buf[30];
    const char *name = "Name: ";
    strcpy(buf, name);
    strcat(buf, getDeviceName());
    display->drawString(64 + x, 48 + y, buf);
}

/// Draw the last text message we received
static void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    MeshPacket &mp = devicestate.rx_text_message;
    NodeInfo *node = nodeDB.getNode(mp.from);
    // DEBUG_MSG("drawing text message from 0x%x: %s\n", mp.from,
    // mp.decoded.variant.data.decoded.bytes);

    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will
    // be wrapped. Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_16);
    String sender = (node && node->has_user) ? node->user.short_name : "???";
    display->drawString(0 + x, 0 + y, sender);
    display->setFont(ArialMT_Plain_10);

    // the max length of this buffer is much longer than we can possibly print
    static char tempBuf[96];
    assert(mp.decoded.which_payload == SubPacket_data_tag);
    snprintf(tempBuf, sizeof(tempBuf), "         %s", mp.decoded.data.payload.bytes);

    display->drawStringMaxWidth(4 + x, 10 + y, 128, tempBuf);
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
        yo += FONT_HEIGHT;
        if (yo > SCREEN_HEIGHT - FONT_HEIGHT) {
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
                yo += FONT_HEIGHT;
                col = 0;
            }
            f++;
        }
        if (col != 0) {
            // Include last incomplete line in our total.
            yo += FONT_HEIGHT;
        }

        return yo;
    }
#endif

// Draw power bars or a charging indicator on an image of a battery, determined by battery charge voltage or percentage.
static void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const PowerStatus *powerStatus) 
{
    static const uint8_t powerBar[3] = { 0x81, 0xBD, 0xBD };
    static const uint8_t lightning[8] = { 0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85 };
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
            if(powerStatus->getBatteryChargePercent() >= 25 * i) 
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
    }
    if (gps->getDOP() <= 100) {
        display->drawString(x + 8, y - 2, "Ideal");
        return;
    }
    if (gps->getDOP() <= 200) {
        display->drawString(x + 8, y - 2, "Exc.");
        return;
    }
    if (gps->getDOP() <= 500) {
        display->drawString(x + 8, y - 2, "Good");
        return;
    }
    if (gps->getDOP() <= 1000) {
        display->drawString(x + 8, y - 2, "Mod.");
        return;
    }
    if (gps->getDOP() <= 2000) {
        display->drawString(x + 8, y - 2, "Fair");
        return;
    }
    if (gps->getDOP() > 0) {
        display->drawString(x + 8, y - 2, "Poor");
        return;
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

// Draw the compass and arrow pointing to location
static void drawCompass(OLEDDisplay *display, int16_t compassX, int16_t compassY, float headingRadian)
{
    // display->drawXbm(compassX, compassY, compass_width, compass_height,
    // (const uint8_t *)compass_bits);

    Point tip(0.0f, 0.5f), tail(0.0f, -0.5f); // pointing up initially
    float arrowOffsetX = 0.2f, arrowOffsetY = 0.2f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y - arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y - arrowOffsetY);

    Point *points[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++) {
        points[i]->rotate(headingRadian);
        points[i]->scale(COMPASS_DIAM * 0.6);
        points[i]->translate(compassX, compassY);
    }
    drawLine(display, tip, tail);
    drawLine(display, leftArrow, tip);
    drawLine(display, rightArrow, tip);

    display->drawCircle(compassX, compassY, COMPASS_DIAM / 2);
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

        // We just changed to a new node screen, ask that node for updated state
        displayedNodeNum = n->num;
        service.sendNetworkPing(displayedNodeNum, true);
    }

    NodeInfo *node = nodeDB.getNodeByIndex(nodeIndex);

    display->setFont(ArialMT_Plain_10);

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
    int16_t compassX = x + SCREEN_WIDTH - COMPASS_DIAM / 2 - 1, compassY = y + SCREEN_HEIGHT / 2;

    if (ourNode && hasPosition(ourNode) && hasPosition(node)) { // display direction toward node
        Position &op = ourNode->position, &p = node->position;
        float d = latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
        if (d < 2000)
            snprintf(distStr, sizeof(distStr), "%.0f m", d);
        else
            snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);

        // FIXME, also keep the guess at the operators heading and add/substract
        // it.  currently we don't do this and instead draw north up only.
        float bearingToOther = bearing(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
        float myHeading = estimatedHeading(DegD(p.latitude_i), DegD(p.longitude_i));
        headingRadian = bearingToOther - myHeading;
        drawCompass(display, compassX, compassY, headingRadian);
    } else { // direction to node is unknown so display question mark
        // Debug info for gps lock errors
        // DEBUG_MSG("ourNode %d, ourPos %d, theirPos %d\n", !!ourNode, ourNode && hasPosition(ourNode), hasPosition(node));

        display->drawString(compassX - FONT_HEIGHT / 4, compassY - FONT_HEIGHT / 2, "?");
        display->drawCircle(compassX, compassY, COMPASS_DIAM / 2);
    }

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

Screen::Screen(uint8_t address, int sda, int scl) : cmdQueue(32), dispdev(address, sda, scl), ui(&dispdev) {}

void Screen::handleSetOn(bool on)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            DEBUG_MSG("Turning on screen\n");
            dispdev.displayOn();
            dispdev.displayOn();
        } else {
            DEBUG_MSG("Turning off screen\n");
            dispdev.displayOff();
        }
        screenOn = on;
    }
}

void Screen::setup()
{
    PeriodicTask::setup();

    // We don't set useDisplay until setup() is called, because some boards have a declaration of this object but the device
    // is never found when probing i2c and therefore we don't call setup and never want to do (invalid) accesses to this device.
    useDisplay = true;

    dispdev.resetOrientation();

    // Initialising the UI will init the display too.
    ui.init();
    ui.setTimePerTransition(300); // msecs
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

#ifdef FLIP_SCREEN_VERTICALLY
    dispdev.flipScreenVertically();
#endif

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
}

void Screen::doTask()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        setPeriod(0);
        return;
    }

    // Process incoming commands.
    for (;;) {
        CmdItem cmd;
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
        case Cmd::STOP_BLUETOOTH_PIN_SCREEN:
        case Cmd::STOP_BOOT_SCREEN:
            setFrames();
            break;
        case Cmd::PRINT:
            handlePrint(cmd.print_text);
            free(cmd.print_text);
            break;
        default:
            DEBUG_MSG("BUG: invalid cmd");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        setPeriod(0);
        return;
    }

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.
    if (targetFramerate != IDLE_FRAMERATE && ui.getUiState()->frameState == FIXED) {
        // oldFrameState = ui.getUiState()->frameState;
        DEBUG_MSG("Setting idle framerate\n");
        targetFramerate = IDLE_FRAMERATE;
        ui.setTargetFPS(targetFramerate);
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
    }

    ui.update();

    // DEBUG_MSG("want fps %d, fixed=%d\n", targetFramerate,
    // ui.getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
    setPeriod(1000 / targetFramerate);
}

void Screen::drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen = reinterpret_cast<Screen *>(state->userData);
    screen->debugInfo.drawFrame(display, state, x, y);
}

// restore our regular frame list
void Screen::setFrames()
{
    DEBUG_MSG("showing standard frames\n");
    showingNormalScreen = true;

    // We don't show the node info our our node (if we have it yet - we should)
    size_t numnodes = nodeStatus->getNumTotal();
    if (numnodes > 0)
        numnodes--;

    size_t numframes = 0;

    // If we have a text message - show it first
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

    ui.setFrames(normalFrames, numframes);
    ui.enableAllIndicators();

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list
                    // just changed)
}

void Screen::handleStartBluetoothPinScreen(uint32_t pin)
{
    DEBUG_MSG("showing bluetooth screen\n");
    showingNormalScreen = false;

    static FrameCallback btFrames[] = {drawFrameBluetooth};

    snprintf(btPIN, sizeof(btPIN), "%06u", pin);

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
}

void Screen::handlePrint(const char *text)
{
    DEBUG_MSG("Screen: %s", text);
    if (!useDisplay)
        return;

    dispdev.print(text);
}

void Screen::handleOnPress()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui.getUiState()->frameState == FIXED) {
        setPeriod(1); // redraw ASAP
        ui.nextFrame();

        DEBUG_MSG("Setting fast framerate\n");

        // We are about to start a transition so speed up fps
        targetFramerate = TRANSITION_FRAMERATE;
        ui.setTargetFPS(targetFramerate);
    }
}

void DebugInfo::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(ArialMT_Plain_10);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    char channelStr[20];
    {
        LockGuard guard(&lock);
        snprintf(channelStr, sizeof(channelStr), "#%s", channelName.c_str());

        // Display power status
        if (powerStatus->getHasBattery())
            drawBattery(display, x, y + 2, imgBattery, powerStatus);
        else
            display->drawFastImage(x, y + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
        // Display nodes status
        drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 2, nodeStatus);
        // Display GPS status
        drawGPS(display, x + (SCREEN_WIDTH * 0.66), y + 2, gpsStatus);
    }

    display->drawString(x, y + FONT_HEIGHT, channelStr);

    display->drawLogBuffer(x, y + (FONT_HEIGHT * 2));

    /* Display a heartbeat pixel that blinks every time the frame is redrawn
    if(heartbeat) display->setPixel(0, 0);
    heartbeat = !heartbeat;
    */
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

int Screen::handleStatusUpdate(const Status *arg) {
    DEBUG_MSG("Screen got status update %d\n", arg->getStatusType());
    switch(arg->getStatusType())
    {
        case STATUS_TYPE_NODE:
            setFrames();
            break;
    }
    setPeriod(1); // Update the screen right away
    return 0;
}
} // namespace meshtastic
