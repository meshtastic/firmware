#include "configuration.h"

#ifdef HAS_EINK
#include "EInkDisplay2.h"
#include "SPILock.h"
#include <SPI.h>
#include "GxEPD2_BW.h"

#define COLORED GxEPD_BLACK
#define UNCOLORED GxEPD_WHITE


#define TECHO_DISPLAY_MODEL GxEPD2_154_D67

GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT> *adafruitDisplay;

EInkDisplay::EInkDisplay(uint8_t address, int sda, int scl)
{
    setGeometry(GEOMETRY_RAWMODE, TECHO_DISPLAY_MODEL::WIDTH, TECHO_DISPLAY_MODEL::HEIGHT);
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

    if (adafruitDisplay && (sinceLast > msecLimit || lastDrawMsec == 0)) {
        lastDrawMsec = now;

        // FIXME - only draw bits have changed (use backbuf similar to the other displays)
        // tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
        for (uint8_t y = 0; y < displayHeight; y++) {
            for (uint8_t x = 0; x < displayWidth; x++) {

                // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
                auto b = buffer[x + (y / 8) * displayWidth];
                auto isset = b & (1 << (y & 7));
                adafruitDisplay->drawPixel(x, y, isset ? COLORED : UNCOLORED);
            }
        }

        DEBUG_MSG("Updating eink... ");
        // ePaper.Reset(); // wake the screen from sleep
        adafruitDisplay->display(false); // FIXME, use partial update mode
        // Put screen to sleep to save power (possibly not necessary because we already did poweroff inside of display)
        adafruitDisplay->hibernate();
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

    auto lowLevel = new TECHO_DISPLAY_MODEL(PIN_EINK_CS,
                                                            PIN_EINK_DC,
                                                            PIN_EINK_RES,
                                                            PIN_EINK_BUSY, SPI1);

    adafruitDisplay = new GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT>(*lowLevel);
    adafruitDisplay->init();
    adafruitDisplay->setRotation(3);
    //adafruitDisplay->setFullWindow();
    //adafruitDisplay->fillScreen(UNCOLORED);
    //adafruitDisplay->drawCircle(100, 100, 20, COLORED);
    //adafruitDisplay->display(false);

    return true;
}

#endif
