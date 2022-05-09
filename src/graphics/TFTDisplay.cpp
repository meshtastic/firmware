#include "configuration.h"

#if defined(ST7735_CS) || defined(ILI9341_DRIVER)
#include "SPILock.h"
#include "TFTDisplay.h"
#include <SPI.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip

static TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

TFTDisplay::TFTDisplay(uint8_t address, int sda, int scl)
{
#ifdef SCREEN_ROTATE
    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);
#else
    setGeometry(GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
}

// Write the buffer to the display memory
void TFTDisplay::display(void)
{
    concurrency::LockGuard g(spiLock);

    // FIXME - only draw bits have changed (use backbuf similar to the other displays)
    // tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
    for (uint16_t y = 0; y < displayHeight; y++) {
        for (uint16_t x = 0; x < displayWidth; x++) {
            // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
            auto isset = buffer[x + (y / 8) * displayWidth] & (1 << (y & 7));
            tft.drawPixel(x, y, isset ? TFT_WHITE : TFT_BLACK);
        }
    }
}

// Send a command to the display (low level function)
void TFTDisplay::sendCommand(uint8_t com)
{
    (void)com;
    // Drop all commands to device (we just update the buffer)
}

void TFTDisplay::setDetected(uint8_t detected)
{
    (void)detected;
}

// Connect to the display
bool TFTDisplay::connect()
{
    concurrency::LockGuard g(spiLock);
    DEBUG_MSG("Doing TFT init\n");

#ifdef TFT_BL
    digitalWrite(TFT_BL, HIGH);
    pinMode(TFT_BL, OUTPUT);
#endif

#ifdef ST7735_BACKLIGHT_EN
    digitalWrite(ST7735_BACKLIGHT_EN, HIGH);
    pinMode(ST7735_BACKLIGHT_EN, OUTPUT);
#endif
    tft.init();
#ifdef M5STACK
    tft.setRotation(1); // M5Stack has the TFT in landscape
#else
    tft.setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
#endif
    tft.fillScreen(TFT_BLACK);
    // tft.drawRect(0, 0, 40, 10, TFT_PURPLE); // wide rectangle in upper left
    return true;
}

#endif
