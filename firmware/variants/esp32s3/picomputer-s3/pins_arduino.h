#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 8;
static const uint8_t SCL = 9;

// Default SPI
static const uint8_t MISO = 39;
static const uint8_t SCK = 21;
static const uint8_t MOSI = 38;
static const uint8_t SS = 40;

#endif /* Pins_Arduino_h */