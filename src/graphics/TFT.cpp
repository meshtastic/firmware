#include "configuration.h"

#ifdef ST7735_CS

#include "TFT.h"
#include <SPI.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h


TFTDisplay::TFTDisplay(uint8_t address, int sda, int scl)
{
    setGeometry(
        GEOMETRY_128_64); // FIXME - currently we lie and claim 128x64 because I'm not yet sure other resolutions will work
}

// Write the buffer to the display memory
void TFTDisplay::display(void)
{
    // FIXME - only draw bits have changed (use backbuf similar to the other displays)
    tft.drawBitmap(0, 0, buffer, 128, 64, TFT_YELLOW, TFT_BLACK);
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

    tft.init();
    tft.setRotation(3); // Orient horizontal and wide underneath the silkscreen name label
    tft.fillScreen(TFT_BLUE);
    // tft.drawRect(0, 0, 40, 10, TFT_PURPLE); // wide rectangle in upper left

    return true;
}

#endif
