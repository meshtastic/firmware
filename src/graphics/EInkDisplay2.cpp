#include "configuration.h"

#ifdef HAS_EINK2
#include "EInkDisplay.h"
#include "SPILock.h"
#include <SPI.h>

#define COLORED 0
#define UNCOLORED 1

#define INK COLORED     // Black ink
#define PAPER UNCOLORED // 'paper' background colour

#define EPD_WIDTH 200 // FIXME
#define EPD_HEIGHT 200

EInkDisplay::EInkDisplay(uint8_t address, int sda, int scl)
{
    setGeometry(GEOMETRY_RAWMODE, EPD_WIDTH, EPD_HEIGHT);
    // setGeometry(GEOMETRY_RAWMODE, 128, 64); // old resolution
    // setGeometry(GEOMETRY_128_64); // We originally used this because I wasn't sure if rawmode worked - it does
}

// FIXME quick hack to limit drawing to a very slow rate
uint32_t lastDrawMsec;

/**
 * Force a display update if we haven't drawn within the specified msecLimit
 */
bool EInkDisplay::forceDisplay(uint32_t msecLimit)
{
    // No need to grab this lock because we are on our own SPI bus
    // concurrency::LockGuard g(spiLock);

    uint32_t now = millis();
    uint32_t sinceLast = now - lastDrawMsec;

    if (sinceLast > msecLimit || lastDrawMsec == 0) {
        lastDrawMsec = now;

        // FIXME - only draw bits have changed (use backbuf similar to the other displays)
        // tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
        for (uint8_t y = 0; y < displayHeight; y++) {
            for (uint8_t x = 0; x < displayWidth; x++) {

                // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
                auto b = buffer[x + (y / 8) * displayWidth];
                auto isset = b & (1 << (y & 7));
                // frame.drawPixel(x, y, isset ? INK : PAPER);
            }
        }

        DEBUG_MSG("Updating eink... ");
        // ePaper.Reset(); // wake the screen from sleep
        // updateDisplay(); // Send image to display and refresh
        // Put screen to sleep to save power
        // ePaper.Sleep();
        DEBUG_MSG("done\n");

        return true;
    } else {
        // DEBUG_MSG("Skipping eink display\n");
        return false;
    }
}

// Write the buffer to the display memory
void EInkDisplay::display(void)
{
    // We don't allow regular 'dumb' display() calls to draw on eink until we've shown
    // at least one forceDisplay() keyframe.  This prevents flashing when we should the critical
    // bootscreen (that we want to look nice)
    if (lastDrawMsec)
        forceDisplay(slowUpdateMsec); // Show the first screen a few seconds after boot, then slower
}

// Send a command to the display (low level function)
void EInkDisplay::sendCommand(uint8_t com)
{
    (void)com;
    // Drop all commands to device (we just update the buffer)
}

// Connect to the display
bool EInkDisplay::connect()
{
    DEBUG_MSG("Doing EInk init\n");

#ifdef PIN_EINK_PWR_ON
    digitalWrite(PIN_EINK_PWR_ON, HIGH); // If we need to assert a pin to power external peripherals
    pinMode(PIN_EINK_PWR_ON, OUTPUT);
#endif

#ifdef PIN_EINK_EN
    // backlight power, HIGH is backlight on, LOW is off
    digitalWrite(PIN_EINK_EN, LOW);
    pinMode(PIN_EINK_EN, OUTPUT);
#endif

    // Initialise the ePaper library
    // FIXME - figure out how to use lut_partial_update
    if (false) {
        DEBUG_MSG("ePaper init failed\n");
        return false;
    } else {
        return true;
    }
}

#endif
