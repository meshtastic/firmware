#include "variant.h"
#include "Arduino.h"
#include "Wire.h"
#include <esp32-hal-periman.h>

void initVariant(void)
{
    // initialize display
    pinMode(PIN_LCD_GPIO_BLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_GPIO_BLIGHT, LOW);
    pinMode(PIN_LCD_GPIO_RST, OUTPUT);
    digitalWrite(PIN_LCD_GPIO_RST, LOW);
    pinMode(PIN_DISPLAY_CHIP_POWER, OUTPUT);
    digitalWrite(PIN_DISPLAY_CHIP_POWER, !DISPLAY_CHIP_POWER_ON_LEVEL);
    pinMode(PIN_DISPLAY_PANEL_POWER, OUTPUT);
    digitalWrite(PIN_DISPLAY_PANEL_POWER, !DISPLAY_PANEL_POWER_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(PIN_DISPLAY_CHIP_POWER, DISPLAY_CHIP_POWER_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(PIN_DISPLAY_PANEL_POWER, DISPLAY_PANEL_POWER_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(PIN_LCD_GPIO_RST, HIGH);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Power and reset the GT911 before the initial I2C scan. INT low selects address 0x5D.
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    pinMode(SCREEN_TOUCH_INT, OUTPUT);
    digitalWrite(SCREEN_TOUCH_INT, LOW);
    pinMode(PIN_TOUCH_POWER, OUTPUT);
    digitalWrite(PIN_TOUCH_POWER, !TOUCH_POWER_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(PIN_TOUCH_POWER, TOUCH_POWER_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(PIN_TOUCH_RST, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
    pinMode(SCREEN_TOUCH_INT, INPUT_PULLUP);

    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);

    pinMode(AUDIO_AMP_CTRL, OUTPUT);
    digitalWrite(AUDIO_AMP_CTRL, AUDIO_POWER_DISABLE);
}
