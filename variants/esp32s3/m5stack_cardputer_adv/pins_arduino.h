#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// Some boards have too low voltage on this pin (board design bug)
// Use different pin with 3V and connect with 48
// and change this setup for the chosen pin (for example 38)
// static const uint8_t LED_BUILTIN = SOC_GPIO_PIN_COUNT + 48;
// #define BUILTIN_LED LED_BUILTIN // backward compatibility
// #define LED_BUILTIN LED_BUILTIN
// #define RGB_BUILTIN LED_BUILTIN
// #define RGB_BRIGHTNESS 64

static const uint8_t TXD2 = 13;
static const uint8_t RXD2 = 15;

static const uint8_t SDA = 8;
static const uint8_t SCL = 9;

// external & microSD
static const uint8_t SS = 5; // external SS
static const uint8_t MOSI = 14;
static const uint8_t MISO = 39;
static const uint8_t SCK = 40;

static const uint8_t ADC = 10;

#endif /* Pins_Arduino_h */
