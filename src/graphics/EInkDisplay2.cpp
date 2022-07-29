#include "configuration.h"

#ifdef USE_EINK
#include "main.h"
#include "EInkDisplay2.h"
#include "SPILock.h"
#include <SPI.h>
#include "GxEPD2_BW.h"

#define COLORED GxEPD_BLACK
#define UNCOLORED GxEPD_WHITE

#if defined(TTGO_T_ECHO)
#define TECHO_DISPLAY_MODEL GxEPD2_154_D67
#elif defined(RAK4630)

//GxEPD2_213_B74 - RAK14000 2.13 inch b/w 250x128 
#define TECHO_DISPLAY_MODEL GxEPD2_213_B74

//4.2 inch 300x400 - GxEPD2_420_M01
//#define TECHO_DISPLAY_MODEL GxEPD2_420_M01

//2.9 inch 296x128 - GxEPD2_290_T5D
//#define TECHO_DISPLAY_MODEL GxEPD2_290_T5D

//1.54 inch 200x200 - GxEPD2_154_M09
//#define TECHO_DISPLAY_MODEL GxEPD2_154_M09

#elif defined(PCA10059)

//4.2 inch 300x400 - GxEPD2_420_M01
#define TECHO_DISPLAY_MODEL GxEPD2_420_M01

#elif defined(M5_COREINK)
//M5Stack CoreInk
//1.54 inch 200x200 - GxEPD2_154_M09
#define TECHO_DISPLAY_MODEL GxEPD2_154_M09

#endif

GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT> *adafruitDisplay;

EInkDisplay::EInkDisplay(uint8_t address, int sda, int scl)
{
    #if defined(TTGO_T_ECHO)
    setGeometry(GEOMETRY_RAWMODE, TECHO_DISPLAY_MODEL::WIDTH, TECHO_DISPLAY_MODEL::HEIGHT);
    #elif defined(RAK4630)
    
    //GxEPD2_213_B74 - RAK14000 2.13 inch b/w 250x128 
    setGeometry(GEOMETRY_RAWMODE, 250, 122);
    
    //GxEPD2_420_M01
    //setGeometry(GEOMETRY_RAWMODE, 300, 400);

    //GxEPD2_290_T5D
    //setGeometry(GEOMETRY_RAWMODE, 296, 128);

    //GxEPD2_154_M09
    //setGeometry(GEOMETRY_RAWMODE, 200, 200);
    
    #elif defined(PCA10059)

    //GxEPD2_420_M01
    setGeometry(GEOMETRY_RAWMODE, 300, 400);
    
    #elif defined(M5_COREINK)
    
    //M5Stack_CoreInk 200x200
    //1.54 inch 200x200 - GxEPD2_154_M09
    setGeometry(GEOMETRY_RAWMODE, EPD_HEIGHT, EPD_WIDTH);

    #endif
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
        for (uint32_t y = 0; y < displayHeight; y++) {
            for (uint32_t x = 0; x < displayWidth; x++) {

                // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
                auto b = buffer[x + (y / 8) * displayWidth];
                auto isset = b & (1 << (y & 7));
                adafruitDisplay->drawPixel(x, y, isset ? COLORED : UNCOLORED);
            }
        }

        DEBUG_MSG("Updating E-Paper... ");
        
        #if defined(TTGO_T_ECHO)
        // ePaper.Reset(); // wake the screen from sleep
        adafruitDisplay->display(false); // FIXME, use partial update mode
        #elif defined(RAK4630)
        
        //RAK14000 2.13 inch b/w 250x122 does not support partial updates
        adafruitDisplay->display(false); // FIXME, use partial update mode
        
        //Only enable for e-Paper with support for partial updates and comment out above adafruitDisplay->display(false); 
        // 1.54 inch 200x200 - GxEPD2_154_M09
        // 2.9 inch 296x128 - GxEPD2_290_T5D
        // 4.2 inch 300x400 - GxEPD2_420_M01
        //adafruitDisplay->nextPage();
        
        #elif defined(PCA10059) || defined(M5_COREINK)
        adafruitDisplay->nextPage();
        #endif
        
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

void EInkDisplay::setDetected(uint8_t detected)
{
    (void)detected;
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

    
#if defined(TTGO_T_ECHO)
{
    auto lowLevel = new TECHO_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY, SPI1);

    adafruitDisplay = new GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT>(*lowLevel);
    adafruitDisplay->init();
    adafruitDisplay->setRotation(3);
}
#elif defined(RAK4630)
{
    if (eink_found) {
        auto lowLevel = new TECHO_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);

        adafruitDisplay = new GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT>(*lowLevel);
        
        adafruitDisplay->init(115200, true, 10, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));

        //RAK14000 2.13 inch b/w 250x122 does not support partial updates 
        adafruitDisplay->setRotation(3);
        //For 1.54, 2.9 and 4.2
        //adafruitDisplay->setRotation(1);
        //adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
    } else {
        (void)adafruitDisplay;
    }      
}
#elif defined(PCA10059)
{
    auto lowLevel = new TECHO_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
    adafruitDisplay = new GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT>(*lowLevel);
    adafruitDisplay->init(115200, true, 10, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    adafruitDisplay->setRotation(3);
    adafruitDisplay->setPartialWindow(0, 0, displayWidth, displayHeight);
}
#elif defined(M5_COREINK)
    auto lowLevel = new TECHO_DISPLAY_MODEL(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY);
    adafruitDisplay = new GxEPD2_BW<TECHO_DISPLAY_MODEL, TECHO_DISPLAY_MODEL::HEIGHT>(*lowLevel);
    adafruitDisplay->init(115200, true, 40, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    adafruitDisplay->setRotation(0);
    adafruitDisplay->setPartialWindow(0, 0, EPD_WIDTH, EPD_HEIGHT);
#endif
   
    
    //adafruitDisplay->setFullWindow();
    //adafruitDisplay->fillScreen(UNCOLORED);
    //adafruitDisplay->drawCircle(100, 100, 20, COLORED);
    //adafruitDisplay->display(false);

    return true;
}

#endif
