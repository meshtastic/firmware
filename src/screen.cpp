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

#define SCREEN_HEADER_HEIGHT 14

#ifdef I2C_SDA
SSD1306Wire  dispdev(SSD1306_ADDRESS, I2C_SDA, I2C_SCL);
#else
SSD1306Wire  dispdev(SSD1306_ADDRESS, 0, 0); // fake values to keep build happy, we won't ever init
#endif 

bool disp; // true if we are using display
uint8_t _screen_line = SCREEN_HEADER_HEIGHT - 1;

OLEDDisplayUi ui(&dispdev);

void msOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(128, 0, String(millis()));
}

void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    display->drawXbm(x + 34, y + 14, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
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

void drawFrame4(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will be wrapped.
    // Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->drawStringMaxWidth(0 + x, 10 + y, 128, "Lorem ipsum\n dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore.");
}

void drawFrame5(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
}

// This array keeps function pointers to all frames
// frames are the single views that slide in
FrameCallback frames[] = {drawFrame1, drawFrame2, drawFrame3, drawFrame4, drawFrame5};

// how many frames are there?
int frameCount = 5;

// Overlays are statically drawn on top of a frame eg. a clock
OverlayCallback overlays[] = {msOverlay};
int overlaysCount = 1;

void _screen_header()
{
    if (!disp)
        return;

#if 0
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
#endif
}

void screen_show_logo()
{
    if (!disp)
        return;

#if 0
    uint8_t x = (display->getWidth() - TTN_IMAGE_WIDTH) / 2;
    uint8_t y = SCREEN_HEADER_HEIGHT + (display->getHeight() - SCREEN_HEADER_HEIGHT - TTN_IMAGE_HEIGHT) / 2 + 1;
    display->drawXbm(x, y, TTN_IMAGE_WIDTH, TTN_IMAGE_HEIGHT, TTN_IMAGE);
#endif
}

void screen_off()
{
    if (!disp)
        return;

    dispdev.displayOff();
}

void screen_on()
{
    if (!disp)
        return;

    dispdev.displayOn();
}

static void screen_print(const char *text, uint8_t x, uint8_t y, uint8_t alignment)
{
    DEBUG_MSG(text);

    if (!disp)
        return;

    dispdev.setTextAlignment((OLEDDISPLAY_TEXT_ALIGNMENT)alignment);
    dispdev.drawString(x, y, text);
}

static void screen_print(const char *text, uint8_t x, uint8_t y)
{
    screen_print(text, x, y, TEXT_ALIGN_LEFT);
}

void screen_print(const char *text)
{
    DEBUG_MSG("Screen: %s\n", text);
    if (!disp)
        return;

    dispdev.print(text);
    if (_screen_line + 8 > dispdev.getHeight())
    {
        // scroll
    }
    _screen_line += 8;
    screen_loop();
}

void screen_setup()
{
#ifdef I2C_SDA
    // Display instance
    disp = true;

    // The ESP is capable of rendering 60fps in 80Mhz mode
    // but that won't give you much time for anything else
    // run it in 160Mhz mode or just set it to 30 fps
    ui.setTargetFPS(30);

    // Customize the active and inactive symbol
    //ui.setActiveSymbol(activeSymbol);
    //ui.setInactiveSymbol(inactiveSymbol);

    // You can change this to
    // TOP, LEFT, BOTTOM, RIGHT
    ui.setIndicatorPosition(BOTTOM);

    // Defines where the first frame is located in the bar.
    ui.setIndicatorDirection(LEFT_RIGHT);

    // You can change the transition that is used
    // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_UP, SLIDE_DOWN
    ui.setFrameAnimation(SLIDE_LEFT);

    // Add frames
    ui.setFrames(frames, frameCount);

    // Add overlays
    ui.setOverlays(overlays, overlaysCount);

    // Initialising the UI will init the display too.
    ui.init();

    // Scroll buffer
    dispdev.setLogBuffer(5, 30);

    dispdev.flipScreenVertically();
    dispdev.setFont(Custom_ArialMT_Plain_10);
#endif
}

void screen_loop()
{
    if (!disp)
        return;

#ifdef T_BEAM_V10
    if (axp192_found && pmu_irq)
    {
        pmu_irq = false;
        axp.readIRQ();
        if (axp.isChargingIRQ())
        {
            baChStatus = "Charging";
        }
        else
        {
            baChStatus = "No Charging";
        }
        if (axp.isVbusRemoveIRQ())
        {
            baChStatus = "No Charging";
        }
        // This is not a GPIO actually connected on the tbeam board
        // digitalWrite(2, !digitalRead(2));
        axp.clearIRQ();
    }
#endif

#if 0
    display->clear();
    _screen_header();
    display->drawLogBuffer(0, SCREEN_HEADER_HEIGHT);
    display->display();
#endif
    ui.update();
}
