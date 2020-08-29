#include "configuration.h"

#ifdef ST7735_CS
#include "SPILock.h"
#include "TFT.h"
#include "graphics/configs.h"
#include <SPI.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip

static TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

TFTDisplay::TFTDisplay(uint8_t address, int sda, int scl)
{
    setGeometry(GEOMETRY_128_64); // FIXME - currently we lie and claim 128x64 because I'm not yet sure other resolutions will
                                  // work ie GEOMETRY_RAWMODE
}

// Write the buffer to the display memory
void TFTDisplay::display(void)
{
    concurrency::LockGuard g(spiLock);

#if 1
    // FIXME - only draw bits have changed (use backbuf similar to the other displays)
    // tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
    for (uint8_t y = 0; y < SCREEN_HEIGHT; y++) {
        for (uint8_t x = 0; x < SCREEN_WIDTH; x++) {

            // get src pixel in the page based ordering the OLED lib uses FIXME, super inefficent
            auto b = buffer[x + (y / 8) * SCREEN_WIDTH];
            auto isset = b & (1 << (y & 7));
            tft.drawPixel(x, y, isset ? TFT_WHITE : TFT_BLACK);
        }
    }
#endif
}

// Send a command to the display (low level function)
void TFTDisplay::sendCommand(uint8_t com)
{
    (void)com;
    // Drop all commands to device (we just update the buffer)
}

// Connect to the display
bool TFTDisplay::connect()
{
    DEBUG_MSG("Doing TFT init\n");

#ifdef ST7735_BACKLIGHT_EN
    digitalWrite(ST7735_BACKLIGHT_EN, HIGH);
    pinMode(ST7735_BACKLIGHT_EN, OUTPUT);
#endif

#if 1
    tft.init();
    tft.setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
    tft.fillScreen(TFT_BLACK);
    // tft.drawRect(0, 0, 40, 10, TFT_PURPLE); // wide rectangle in upper left
#endif

    return true;
}

#endif
