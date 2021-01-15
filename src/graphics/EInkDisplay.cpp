#include "configuration.h"

#ifdef HAS_EINK
#include "EInkDisplay.h"
#include "SPILock.h"
#include "epd1in54.h" // Screen specific library
#include <SPI.h>
#include <TFT_eSPI.h> // Graphics library and Sprite class

Epd ePaper; // Create an instance ePaper

TFT_eSPI glc = TFT_eSPI();             // Invoke the graphics library class
TFT_eSprite frame = TFT_eSprite(&glc); // Invoke the Sprite class for the image frame buffer
uint8_t *framePtr;                     // Pointer for the black frame buffer

#define COLORED 0
#define UNCOLORED 1

#define INK COLORED     // Black ink
#define PAPER UNCOLORED // 'paper' background colour

//------------------------------------------------------------------------------------
// Update display - different displays have different function names in the default
// Waveshare libraries  :-(
//------------------------------------------------------------------------------------
#if defined(EPD1IN54B_H) || defined(EPD1IN54C_H) || defined(EPD2IN13B_H) || defined(EPD2IN7B_H) || defined(EPD2IN9B_H) ||        \
    defined(EPD4IN2_H)
void updateDisplay(uint8_t *blackFrame = blackFramePtr, uint8_t *redFrame = redFramePtr)
{
    ePaper.DisplayFrame(blackFrame, redFrame); // Update 3 colour display
#else
void updateDisplay(uint8_t *blackFrame = framePtr)
{
#if defined(EPD2IN7_H) || defined(EPD4IN2_H)
    ePaper.DisplayFrame(blackFrame); // Update 2 color display

#elif defined(EPD1IN54_H) || defined(EPD2IN13_H) || defined(EPD2IN9_H)
    ePaper.SetFrameMemory(blackFrame); // Update 2 colour display
    ePaper.DisplayFrame();
#else
#error "Selected ePaper library is not supported"
#endif
#endif
}

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

    if (framePtr && (sinceLast > msecLimit || lastDrawMsec == 0)) {
        lastDrawMsec = now;

        // FIXME - only draw bits have changed (use backbuf similar to the other displays)
        // tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
        for (uint8_t y = 0; y < displayHeight; y++) {
            for (uint8_t x = 0; x < displayWidth; x++) {

                // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
                auto b = buffer[x + (y / 8) * displayWidth];
                auto isset = b & (1 << (y & 7));
                frame.drawPixel(x, y, isset ? INK : PAPER);
            }
        }

        ePaper.Reset(); // wake the screen from sleep

        DEBUG_MSG("Updating eink... ");
        updateDisplay(); // Send image to display and refresh
        DEBUG_MSG("done\n");

        // Put screen to sleep to save power
        ePaper.Sleep();
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
    digitalWrite(PIN_EINK_EN, HIGH);
    pinMode(PIN_EINK_EN, OUTPUT);
#endif

    // Initialise the ePaper library
    // FIXME - figure out how to use lut_partial_update
    if (ePaper.Init(lut_full_update) != 0) {
        DEBUG_MSG("ePaper init failed\n");
        return false;
    } else {
        frame.setColorDepth(1); // Must set the bits per pixel to 1 for ePaper displays
                                // Set bit depth BEFORE creating Sprite, default is 16!

        // Create a frame buffer in RAM of defined size and save the pointer to it
        // RAM needed is about (EPD_WIDTH * EPD_HEIGHT)/8 , ~5000 bytes for 200 x 200 pixels
        // Note: always create the Sprite before setting the Sprite rotation
        framePtr = (uint8_t *)frame.createSprite(EPD_WIDTH, EPD_HEIGHT);

        frame.fillSprite(PAPER); // Fill frame with white
        /* frame.drawLine(0, 0, frame.width() - 1, frame.height() - 1, INK);
        frame.drawLine(0, frame.height() - 1, frame.width() - 1, 0, INK);
        updateDisplay(); */
        return true;
    }
}

#endif
