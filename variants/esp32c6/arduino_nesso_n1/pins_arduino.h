#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x1001

static const uint8_t TX = -1;
static const uint8_t RX = -1;

static const uint8_t SDA = 10;
static const uint8_t SCL = 8;

// Default SPI will be mapped to Radio
static const uint8_t MOSI = 21;
static const uint8_t MISO = 22;
static const uint8_t SCK = 20;
static const uint8_t SS = 23;

#endif /* Pins_Arduino_h */

