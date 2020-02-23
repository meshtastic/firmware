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

#include <Wire.h>
#include "SSD1306Wire.h"
#include "OLEDDisplay.h"
#include "images.h"
#include "fonts.h"
#include "GPS.h"
#include "OLEDDisplayUi.h"
#include "screen.h"
#include "mesh-pb-constants.h"
#include "NodeDB.h"

#define FONT_HEIGHT 14 // actually 13 for "ariel 10" but want a little extra space
#define FONT_HEIGHT_16 (ArialMT_Plain_16[1] + 1)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#ifdef I2C_SDA
SSD1306Wire dispdev(SSD1306_ADDRESS, I2C_SDA, I2C_SCL);
#else
SSD1306Wire dispdev(SSD1306_ADDRESS, 0, 0); // fake values to keep build happy, we won't ever init
#endif

bool disp;     // true if we are using display
bool screenOn; // true if the display is currently powered

OLEDDisplayUi ui(&dispdev);

#define NUM_EXTRA_FRAMES 2 // text message and debug frame
// A text message frame + debug frame + all the node infos
FrameCallback nonBootFrames[MAX_NUM_NODES + NUM_EXTRA_FRAMES];

Screen screen;
static bool showingBluetooth;

/// If set to true (possibly from an ISR), we should turn on the screen the next time our idle loop runs.
static bool wakeScreen;
static bool showingBootScreen = true; // start by showing the bootscreen

uint32_t lastPressMs;

bool Screen::isOn() { return screenOn; }

void msOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(128, 0, String(millis()));
}

void drawBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    display->drawXbm(x + 32, y, icon_width, icon_height, (const uint8_t *)icon_bits);

    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, SCREEN_HEIGHT - FONT_HEIGHT_16, "meshtastic.org");

    ui.disableIndicator();
}

static char btPIN[16] = "888888";

void drawFrameBluetooth(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Demonstrates the 3 included default sizes. The fonts come from SSD1306Fonts.h file
    // Besides the default fonts there will be a program to convert TrueType fonts into this format
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_16);
    display->drawString(64 + x, 2 + y, "Bluetooth");

    display->setFont(ArialMT_Plain_10);
    display->drawString(64 + x, SCREEN_HEIGHT - FONT_HEIGHT + y, "Enter this code");

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_24);
    display->drawString(64 + x, 22 + y, btPIN);

    ui.disableIndicator();
}

void drawFrame2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Demonstrates the 3 included default sizes. The fonts come from SSD1306Fonts.h file
    // Besides the default fonts there will be a program to convert TrueType fonts into this format
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(0 + x, 10 + y, "Arial 10");

    display->setFont(ArialMT_Plain_16);
    display->drawString(0 + x, 20 + y, "Arial 16");

    display->setFont(ArialMT_Plain_24);
    display->drawString(0 + x, 34 + y, "Arial 24");
}

void drawFrame3(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Text alignment demo
    display->setFont(ArialMT_Plain_10);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(0 + x, 11 + y, "Left aligned (0,10)");

    // The coordinates define the center of the text
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, 22 + y, "Center aligned (64,22)");

    // The coordinates define the right end of the text
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(128 + x, 33 + y, "Right aligned (128,33)");
}

/// Draw the last text message we received
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    MeshPacket &mp = devicestate.rx_text_message;
    NodeInfo *node = nodeDB.getNode(mp.from);

    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will be wrapped.
    // Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_16);
    String sender = (node && node->has_user) ? node->user.short_name : "???";
    display->drawString(0 + x, 0 + y, sender);
    display->setFont(ArialMT_Plain_10);

    static char tempBuf[96];
    snprintf(tempBuf, sizeof(tempBuf), "         %s", mp.payload.variant.data.payload.bytes); // the max length of this buffer is much longer than we can possibly print

    display->drawStringMaxWidth(4 + x, 10 + y, 128, tempBuf);

    // ui.disableIndicator();
}

/// Draw a series of fields in a column, wrapping to multiple colums if needed
void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
{
    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char **f = fields;
    int xo = x, yo = y;
    while (*f)
    {
        display->drawString(xo, yo, *f);
        yo += FONT_HEIGHT;
        if (yo > SCREEN_HEIGHT - FONT_HEIGHT)
        {
            xo += SCREEN_WIDTH / 2;
            yo = 0;
        }
        f++;
    }
}

/// Draw a series of fields in a row, wrapping to multiple rows if needed
/// @return the max y we ended up printing to
uint32_t drawRows(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
{
    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char **f = fields;
    int xo = x, yo = y;
    while (*f)
    {
        display->drawString(xo, yo, *f);
        xo += SCREEN_WIDTH / 2; // hardwired for two columns per row....
        if (xo >= SCREEN_WIDTH)
        {
            yo += FONT_HEIGHT;
            xo = 0;
        }
        f++;
    }

    yo += FONT_HEIGHT; // include the last line in our total

    return yo;
}

/// Ported from my old java code, returns distance in meters along the globe surface (by magic?)
float latLongToMeter(double lat_a, double lng_a, double lat_b, double lng_b)
{
    double pk = (180 / 3.14169);
    double a1 = lat_a / pk;
    double a2 = lng_a / pk;
    double b1 = lat_b / pk;
    double b2 = lng_b / pk;
    double cos_b1 = cos(b1);
    double cos_a1 = cos(a1);
    double t1 =
        cos_a1 * cos(a2) * cos_b1 * cos(b2);
    double t2 =
        cos_a1 * sin(a2) * cos_b1 * sin(b2);
    double t3 = sin(a1) * sin(b1);
    double tt = acos(t1 + t2 + t3);
    if (isnan(tt))
        tt = 0.0; // Must have been the same point?

    return (float)(6366000 * tt);
}

inline double toRadians(double deg)
{
    return deg * PI / 180;
}

inline double toDegrees(double r)
{
    return r * 180 / PI;
}

/**
     * Computes the bearing in degrees between two points on Earth.  Ported from my old Gaggle android app.
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
float bearing(double lat1, double lon1, double lat2, double lon2)
{
    double lat1Rad = toRadians(lat1);
    double lat2Rad = toRadians(lat2);
    double deltaLonRad = toRadians(lon2 - lon1);
    double y = sin(deltaLonRad) * cos(lat2Rad);
    double x = cos(lat1Rad) * sin(lat2Rad) - (sin(lat1Rad) * cos(lat2Rad) * cos(deltaLonRad));
    return atan2(y, x);
}

/// A basic 2D point class for drawing
class Point
{
public:
    float x, y;

    Point(float _x, float _y) : x(_x), y(_y) {}

    /// Apply a rotation around zero (standard rotation matrix math)
    void rotate(float radian)
    {
        float cos = cosf(radian),
              sin = sinf(radian);
        float rx = x * cos - y * sin,
              ry = x * sin + y * cos;

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

void drawLine(OLEDDisplay *d, const Point &p1, const Point &p2)
{
    d->drawLine(p1.x, p1.y, p2.x, p2.y);
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 * 
 * We keep a series of "after you've gone 10 meters, what is your heading since the last reference point?"
 */
float estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0)
    {
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

/// Sometimes we will have Position objects that only have a time, so check for valid lat/lon
bool hasPosition(NodeInfo *n)
{
    return n->has_position && (n->position.latitude != 0 || n->position.longitude != 0);
}
#define COMPASS_DIAM 44

/// We will skip one node - the one for us, so we just blindly loop over all nodes
static size_t nodeIndex;
static int8_t prevFrame = -1;

void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // We only advance our nodeIndex if the frame # has changed - because drawNodeInfo will be called repeatedly while the frame is shown
    if (state->currentFrame != prevFrame)
    {
        prevFrame = state->currentFrame;

        nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
        NodeInfo *n = nodeDB.getNodeByIndex(nodeIndex);
        if (n->num == nodeDB.getNodeNum())
        {
            // Don't show our node, just skip to next
            nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
        }
    }

    NodeInfo *node = nodeDB.getNodeByIndex(nodeIndex);

    display->setFont(ArialMT_Plain_10);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char *username = node->has_user ? node->user.long_name : "Unknown Name";

    static char signalStr[20];
    snprintf(signalStr, sizeof(signalStr), "Signal: %d", node->snr);

    uint32_t agoSecs = sinceLastSeen(node);
    static char lastStr[20];
    if (agoSecs < 120) // last 2 mins?
        snprintf(lastStr, sizeof(lastStr), "%d seconds ago", agoSecs);
    else if (agoSecs < 120 * 60) // last 2 hrs
        snprintf(lastStr, sizeof(lastStr), "%d minutes ago", agoSecs / 60);
    else
        snprintf(lastStr, sizeof(lastStr), "%d hours ago", agoSecs / 60 / 60);

    static float simRadian;
    simRadian += 0.1; // For testing, have the compass spin unless both locations are valid

    static char distStr[20];
    *distStr = 0; // might not have location data
    float headingRadian = simRadian;
    NodeInfo *ourNode = nodeDB.getNode(nodeDB.getNodeNum());
    if (ourNode && hasPosition(ourNode) && hasPosition(node))
    {
        Position &op = ourNode->position, &p = node->position;
        float d = latLongToMeter(p.latitude, p.longitude, op.latitude, op.longitude);
        if (d < 2000)
            snprintf(distStr, sizeof(distStr), "%.0f m", d);
        else
            snprintf(distStr, sizeof(distStr), "%.1f km", d / 1000);

        // FIXME, also keep the guess at the operators heading and add/substract it.  currently we don't do this and instead draw north up only.
        float bearingToOther = bearing(p.latitude, p.longitude, op.latitude, op.longitude);
        float myHeading = estimatedHeading(p.latitude, p.longitude);
        headingRadian = bearingToOther - myHeading;
    }

    const char *fields[] = {
        username,
        distStr,
        signalStr,
        lastStr,
        NULL};
    drawColumns(display, x, y, fields);

    // coordinates for the center of the compass
    int16_t compassX = x + SCREEN_WIDTH - COMPASS_DIAM / 2 - 1, compassY = y + SCREEN_HEIGHT / 2;
    // display->drawXbm(compassX, compassY, compass_width, compass_height, (const uint8_t *)compass_bits);

    Point tip(0.0f, 0.5f), tail(0.0f, -0.5f); // pointing up initially
    float arrowOffsetX = 0.2f, arrowOffsetY = 0.2f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y - arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y - arrowOffsetY);

    Point *points[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++)
    {
        points[i]->rotate(headingRadian);
        points[i]->scale(COMPASS_DIAM * 0.6);
        points[i]->translate(compassX, compassY);
    }
    drawLine(display, tip, tail);
    drawLine(display, leftArrow, tip);
    drawLine(display, rightArrow, tip);

    display->drawCircle(compassX, compassY, COMPASS_DIAM / 2);
}

void drawDebugInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(ArialMT_Plain_10);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    static char usersStr[20];
    snprintf(usersStr, sizeof(usersStr), "Users %d/%d", nodeDB.getNumOnlineNodes(), nodeDB.getNumNodes());

    static char channelStr[20];
    snprintf(channelStr, sizeof(channelStr), "%s", channelSettings.name);

    const char *fields[] = {
        "Batt 89%",
        "GPS 75%",
        usersStr,
        channelStr,
        NULL};
    uint32_t yo = drawRows(display, x, y, fields);

    display->drawLogBuffer(x, yo);
}

// This array keeps function pointers to all frames
// frames are the single views that slide in
FrameCallback bootFrames[] = {drawBootScreen};

// Overlays are statically drawn on top of a frame eg. a clock
OverlayCallback overlays[] = {/* msOverlay */};

// how many frames are there?
const int bootFrameCount = sizeof(bootFrames) / sizeof(bootFrames[0]);
const int overlaysCount = sizeof(overlays) / sizeof(overlays[0]);

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


void Screen::setOn(bool on)
{
    if (!disp)
        return;

    if(on != screenOn) {
        if(on)
    dispdev.displayOn();
    else
    dispdev.displayOff();
    screenOn = on;
    }
}

static void screen_print(const char *text, uint8_t x, uint8_t y, uint8_t alignment)
{
    DEBUG_MSG(text);

    if (!disp)
        return;

    dispdev.setTextAlignment((OLEDDISPLAY_TEXT_ALIGNMENT)alignment);
    dispdev.drawString(x, y, text);
}

void screen_print(const char *text)
{
    DEBUG_MSG("Screen: %s", text);
    if (!disp)
        return;

    dispdev.print(text);
    // ui.update();
}




void Screen::doWakeScreen() {
    wakeScreen = true;
    setPeriod(1); // wake asap
}

void Screen::setup()
{
#ifdef I2C_SDA
    // Display instance
    disp = true;

    // The ESP is capable of rendering 60fps in 80Mhz mode
    // but that won't give you much time for anything else
    // run it in 160Mhz mode or just set it to 30 fps
    // We do this now in loop()
    // ui.setTargetFPS(30);

    // Customize the active and inactive symbol
    //ui.setActiveSymbol(activeSymbol);
    //ui.setInactiveSymbol(inactiveSymbol);

    ui.setTimePerTransition(300); // msecs

    // You can change this to
    // TOP, LEFT, BOTTOM, RIGHT
    ui.setIndicatorPosition(BOTTOM);

    // Defines where the first frame is located in the bar.
    ui.setIndicatorDirection(LEFT_RIGHT);

    // You can change the transition that is used
    // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_UP, SLIDE_DOWN
    ui.setFrameAnimation(SLIDE_LEFT);

    // Add frames - we subtract one from the framecount so there won't be a visual glitch when we take the boot screen out of the sequence.
    ui.setFrames(bootFrames, bootFrameCount);

    // Add overlays
    ui.setOverlays(overlays, overlaysCount);

    // Initialising the UI will init the display too.
    ui.init();

    // Scroll buffer
    dispdev.setLogBuffer(3, 32);

    setOn(true); // update our screenOn bool

#ifdef BICOLOR_DISPLAY
    dispdev.flipScreenVertically(); // looks better without this on lora32
#endif

    // dispdev.setFont(Custom_ArialMT_Plain_10);

    ui.disableAutoTransition(); // we now require presses
#endif
}


/// Turn off the screen this many ms after last press or wake
#define SCREEN_SLEEP_MS (60 * 1000)

#define TRANSITION_FRAMERATE 30 // fps
#define IDLE_FRAMERATE 10       // in fps

static uint32_t targetFramerate = IDLE_FRAMERATE;



void Screen::doTask()
{
    if (!disp) { // If we don't have a screen, don't ever spend any CPU for us
        setPeriod(0);
        return;
    }

    if (wakeScreen) // If a new text message arrived, turn the screen on immedately
    {
        lastPressMs = millis(); // if we were told to wake the screen, reset the press timeout
        screen.setOn(true);            // make sure the screen is not asleep
        wakeScreen = false;
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then stop updating until it is on again
        setPeriod(0);
        return;
    }

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because otherwise that breaks
    // animations.
    if (targetFramerate != IDLE_FRAMERATE && ui.getUiState()->frameState == FIXED)
    {
        // oldFrameState = ui.getUiState()->frameState;
        DEBUG_MSG("Setting idle framerate\n");
        targetFramerate = IDLE_FRAMERATE;
        ui.setTargetFPS(targetFramerate);
    }
    ui.update();

    // While showing the bluetooth pair screen all of our standard screen switching is stopped
    if (!showingBluetooth)
    {
        // Once we finish showing the bootscreen, remove it from the loop
        if (showingBootScreen)
        {
            if (millis() > 3 * 1000) // we show the boot screen for a few seconds only
            {
                showingBootScreen = false;
                screen_set_frames();
            }
        }
        else // standard screen loop handling ehre
        {
            // If the # nodes changes, we need to regen our list of screens
            if (nodeDB.updateGUI || nodeDB.updateTextMessage)
            {
                screen_set_frames();
                nodeDB.updateGUI = false;
                nodeDB.updateTextMessage = false;
            }
        }
    }

    // DEBUG_MSG("want fps %d, fixed=%d\n", targetFramerate, ui.getUiState()->frameState);
    // If we are scrolling we need to be called soon, otherwise just 1 fps (to save CPU)
    // We also ask to be called twice as fast as we really need so that any rounding errors still result
    // with the correct framerate
    setPeriod(1000 / targetFramerate);
}

// Show the bluetooth PIN screen
void screen_start_bluetooth(uint32_t pin)
{
    static FrameCallback btFrames[] = {drawFrameBluetooth};

    snprintf(btPIN, sizeof(btPIN), "%06d", pin);

    DEBUG_MSG("showing bluetooth screen\n");
    showingBluetooth = true;
    screen.doWakeScreen();

    ui.setFrames(btFrames, 1); // Just show the bluetooth frame
    // we rely on our main loop to show this screen (because we are invoked deep inside of bluetooth callbacks)
    // ui.update(); // manually draw once, because I'm not sure if loop is getting called
}

// restore our regular frame list
void screen_set_frames()
{
    DEBUG_MSG("showing standard frames\n");

    size_t numnodes = nodeDB.getNumNodes();
    // We don't show the node info our our node (if we have it yet - we should)
    if (numnodes > 0)
        numnodes--;

    size_t numframes = 0;

    // If we have a text message - show it first
    if (devicestate.has_rx_text_message)
        nonBootFrames[numframes++] = drawTextMessageFrame;

    // then all the nodes
    for (size_t i = 0; i < numnodes; i++)
        nonBootFrames[numframes++] = drawNodeInfo;

    // then the debug info
    nonBootFrames[numframes++] = drawDebugInfo;

    ui.setFrames(nonBootFrames, numframes);
    showingBluetooth = false;

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list just changed)
}

/// handle press of the button
void Screen::onPress()
{
    // screen_start_bluetooth(123456);

    lastPressMs = millis();
    screen.doWakeScreen();

    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (screenOn && ui.getUiState()->frameState == FIXED)
    {
        ui.nextFrame();

        DEBUG_MSG("Setting fast framerate\n");

        // We are about to start a transition so speed up fps
        targetFramerate = TRANSITION_FRAMERATE;
        ui.setTargetFPS(targetFramerate);
    }
}