#include "configuration.h"

#ifdef ST7735_CS

#include <SPI.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

void TFTinit()
{
    DEBUG_MSG("Doing TFT init\n");

#ifdef ST7735_BACKLIGHT_EN
    digitalWrite(ST7735_BACKLIGHT_EN, HIGH);
    pinMode(ST7735_BACKLIGHT_EN, OUTPUT);
#endif

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_GREEN);
}

#endif
