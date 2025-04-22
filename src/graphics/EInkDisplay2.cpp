#include "configuration.h"

#ifdef USE_EINK
#include "EInkDisplay2.h"
#include "SPILock.h"
#include "main.h"
#include <SPI.h>

/*
    The macros EINK_DISPLAY_MODEL, EINK_WIDTH, and EINK_HEIGHT are defined as build_flags in a variant's platformio.ini
    Previously, these macros were defined at the top of this file.

    For archival reasons, note that the following configurations had also been tested during this period:
    * ifdef RAK4631
        - 4.2 inch
          EINK_DISPLAY_MODEL: GxEPD2_420_M01
          EINK_WIDTH: 300
          EINK_WIDTH: 400

        - 2.9 inch
          EINK_DISPLAY_MODEL: GxEPD2_290_T5D
          EINK_WIDTH: 296
          EINK_HEIGHT: 128

        - 1.54 inch
          EINK_DISPLAY_MODEL: GxEPD2_154_M09
          EINK_WIDTH: 200
          EINK_HEIGHT: 200
*/

// Constructor
EInkDisplay::EInkDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
    // Set dimensions in OLEDDisplay base class
    this->geometry = GEOMETRY_RAWMODE;
    this->displayWidth = EINK_WIDTH;
    this->displayHeight = EINK_HEIGHT;

    // Round shortest side up to nearest byte, to prevent truncation causing an undersized buffer
    uint16_t shortSide = min(EINK_WIDTH, EINK_HEIGHT);
    uint16_t longSide = max(EINK_WIDTH, EINK_HEIGHT);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;

    this->displayBufferSize = longSide * (shortSide / 8);
}

/**
 * Force a display update if we haven't drawn within the specified msecLimit
 */
bool EInkDisplay::forceDisplay(uint32_t msecLimit)
{
    // No need to grab this lock because we are on our own SPI bus
    // concurrency::LockGuard g(spiLock);

    uint32_t now = millis();
    uint32_t sinceLast = now - lastDrawMsec;

    if (adafruitDisplay && (sinceLast > msecLimit || lastDrawMsec == 0))
        lastDrawMsec = now;
    else
        return false;

    // FIXME - only draw bits have changed (use backbuf similar to the other displays)
    const bool flipped = config.display.flip_screen;
    for (uint32_t y = 0; y < displayHeight; y++) {
        for (uint32_t x = 0; x < displayWidth; x++) {
            // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficient
            auto b = buffer[x + (y / 8) * displayWidth];
            auto isset = b & (1 << (y & 7));

            // Handle flip here, rather than with setRotation(),
            // Avoids issues when display width is not a multiple of 8
            if (flipped)
                adafruitDisplay->drawPixel((displayWidth - 1) - x, (displayHeight - 1) - y, isset ? GxEPD_BLACK : GxEPD_WHITE);
            else
                adafruitDisplay->drawPixel(x, y, isset ? GxEPD_BLACK : GxEPD_WHITE);
        }
    }

    // Trigger the refresh in GxEPD2
    LOG_DEBUG("Update E-Paper");
    adafruitDisplay->nextPage();

    // End the update process
    endUpdate();

    LOG_DEBUG("done");
    return true;
}

// End the update process - virtual method, overriden in derived class
void EInkDisplay::endUpdate()
{
    // Power off display hardware, then deep-sleep (Except Wireless Paper V1.1, no deep-sleep)
    adafruitDisplay->hibernate();
}

// Write the buffer to the display memory
void EInkDisplay::display(void)
{
    // We don't allow regular 'dumb' display() calls to draw on eink until we've shown
    // at least one forceDisplay() keyframe.  This prevents flashing when we should the critical
    // bootscreen (that we want to look nice)

    if (lastDrawMsec) {
        forceDisplay(slowUpdateMsec); // Show the first screen a few seconds after boot, then slower
    }
}

// Send a command to the display (low level function)
void EInkDisplay::sendCommand(uint8_t com)
{
    (void)com;
    // Drop all commands to device (we just update the buffer)
}

void EInkDisplay::setDetected(uint8_t detected)
{
    (void)detected;
}

// Connect to the display - variant specific
bool EInkDisplay::connect()
{
    LOG_INFO("Do EInk init");

#ifdef PIN_EINK_EN
    // backlight power, HIGH is backlight on, LOW is off
    pinMode(PIN_EINK_EN, OUTPUT);
#ifdef ELECROW_ThinkNode_M1
    // ThinkNode M1 has a hardware dimmable backlight. Start enabled
    digitalWrite(PIN_EINK_EN, HIGH);
#else
    digitalWrite(PIN_EINK_EN, LOW);
#endif
#endif

#if defined(TTGO_T_ECHO) || defined(ELECROW_ThinkNode_M1)
    {
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, SPI1);

        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        adafruitDisplay->init();
#ifdef ELECROW_ThinkNode_M1
        adafruitDisplay->setRotation(4);
#else
        adafruitDisplay->setRotation(3);
#endif
        adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
    }
#elif defined(MESHLINK)
    {
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, SPI1);

        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        adafruitDisplay->init();
        adafruitDisplay->setRotation(3);
        adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
    }
#elif defined(RAK4630) || defined(MAKERPYTHON)
    {
        if (eink_found) {
            auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
            adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
            adafruitDisplay->init(115200, true, 10, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
            // RAK14000 2.13 inch b/w 250x122 does actually now support fast refresh
            adafruitDisplay->setRotation(3);
            // Fast refresh support for  1.54, 2.13 RAK14000 b/w , 2.9 and 4.2
            // adafruitDisplay->setRotation(1);
            adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
        } else {
            (void)adafruitDisplay;
        }
    }

#elif defined(HELTEC_WIRELESS_PAPER_V1_0) || defined(HELTEC_WIRELESS_PAPER) || defined(HELTEC_VISION_MASTER_E213) ||             \
    defined(HELTEC_VISION_MASTER_E290) || defined(TLORA_T3S3_EPAPER) || defined(CROWPANEL_ESP32S3_5_EPAPER) ||                   \
    defined(CROWPANEL_ESP32S3_4_EPAPER) || defined(CROWPANEL_ESP32S3_2_EPAPER)
    {
        // Start HSPI
        hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS); // SCLK, MISO, MOSI, SS
        // VExt already enabled in setup()
        // RTC GPIO hold disabled in setup()

        // Create GxEPD2 objects
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, *hspi);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);

        // Init GxEPD2
        adafruitDisplay->init();
        adafruitDisplay->setRotation(3);
#if defined(CROWPANEL_ESP32S3_5_EPAPER) || defined(CROWPANEL_ESP32S3_4_EPAPER)
        adafruitDisplay->setRotation(0);
#endif
    }
#elif defined(PCA10059) || defined(ME25LS01)
    {
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        adafruitDisplay->init(115200, true, 40, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
        adafruitDisplay->setRotation(0);
        adafruitDisplay->setPartialWindow(0, 0, EINK_WIDTH, EINK_HEIGHT);
    }
#elif defined(M5_COREINK)
    auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
    adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
    adafruitDisplay->init(115200, true, 40, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    adafruitDisplay->setRotation(0);
    adafruitDisplay->setPartialWindow(0, 0, EINK_WIDTH, EINK_HEIGHT);
#elif defined(my) || defined(ESP32_S3_PICO)
    {
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        adafruitDisplay->init(115200, true, 40, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
        adafruitDisplay->setRotation(1);
        adafruitDisplay->setPartialWindow(0, 0, EINK_WIDTH, EINK_HEIGHT);
    }
#elif defined(HELTEC_MESH_POCKET)
    {
        spi1 = &SPI1;
        spi1->begin();
        // VExt already enabled in setup()
        // RTC GPIO hold disabled in setup()

        // Create GxEPD2 objects
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, *spi1);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);

        // Init GxEPD2
        adafruitDisplay->init();
        adafruitDisplay->setRotation(3);
    }
#endif

    return true;
}

#endif
