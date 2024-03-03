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
    for (uint32_t y = 0; y < displayHeight; y++) {
        for (uint32_t x = 0; x < displayWidth; x++) {
            // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficient
            auto b = buffer[x + (y / 8) * displayWidth];
            auto isset = b & (1 << (y & 7));
            adafruitDisplay->drawPixel(x, y, isset ? GxEPD_BLACK : GxEPD_WHITE);
        }
    }

    LOG_DEBUG("Updating E-Paper... ");

#if false
    // Currently unused; rescued from commented-out line during a refactor
    // Use a meaningful macro here if variant doesn't want fast refresh

    // Full update mode (slow)
    adafruitDisplay->display(false)
#else
    // Fast update mode
    adafruitDisplay->nextPage();
#endif

#ifndef EINK_NO_HIBERNATE // Only hibernate if controller IC will preserve image memory
    // Put screen to sleep to save power (possibly not necessary because we already did poweroff inside of display)
    adafruitDisplay->hibernate();
#endif

    LOG_DEBUG("done\n");
    return true;
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
    LOG_INFO("Doing EInk init\n");

#ifdef PIN_EINK_PWR_ON
    pinMode(PIN_EINK_PWR_ON, OUTPUT);
    digitalWrite(PIN_EINK_PWR_ON, HIGH); // If we need to assert a pin to power external peripherals
#endif

#ifdef PIN_EINK_EN
    // backlight power, HIGH is backlight on, LOW is off
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, LOW);
#endif

#if defined(TTGO_T_ECHO)
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

#elif defined(HELTEC_WIRELESS_PAPER_V1_0) || defined(HELTEC_WIRELESS_PAPER)
    {
        // Is this a normal boot, or a wake from deep sleep?
        esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

        // If waking from sleep, need to reverse rtc_gpio_isolate(), called in cpuDeepSleep()
        // Otherwise, SPI won't work
        if (wakeReason != ESP_SLEEP_WAKEUP_UNDEFINED) {
            // HSPI + other display pins
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_SCLK);
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_DC);
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_RES);
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_BUSY);
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_CS);
            rtc_gpio_hold_dis((gpio_num_t)PIN_EINK_MOSI);
        }

        // Start HSPI
        hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS); // SCLK, MISO, MOSI, SS

        // Enable VExt (ACTIVE LOW)
        // Unsure if called elsewhere first?
        delay(100);
        pinMode(Vext, OUTPUT);
        digitalWrite(Vext, LOW);
        delay(100);

        // Create GxEPD2 objects
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, *hspi);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);

        // Init GxEPD2
        adafruitDisplay->init();
        adafruitDisplay->setRotation(3);
    }
#elif defined(PCA10059)
    {
        auto lowLevel = new EINK_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
        adafruitDisplay = new GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        adafruitDisplay->init(115200, true, 10, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
        adafruitDisplay->setRotation(3);
        adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
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
#endif

    return true;
}

#endif
