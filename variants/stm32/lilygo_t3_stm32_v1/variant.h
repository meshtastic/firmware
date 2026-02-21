#ifndef _VARIANT_LILYGO_T3_STM32_V1_
#define _VARIANT_LILYGO_T3_STM32_V1_

#define USE_STM32WLx

#define LED_PIN PA0  // Green LED
#define USER_LED PA1 // Red LED
#define LED_STATE_ON 1

#define BUTTON_PIN PH3 // BOOT

#define BATTERY_PIN PB3 // BAT_DET
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage

/*
OLED currently unsupported as it is wired in 4-wire SPI mode.
STM32 is also presently built without OLED support.
SPI CS#:  PB12
SPI D/C#: PA8
SPI SCK:  PA5
SPI MOSI: PA7
*/
/*
#ifndef HAS_SCREEN
#define HAS_SCREEN
#endif
#define USE_SSD1306
*/

#define LILYGO_T3_STM32_V1

#endif
