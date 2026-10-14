#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x1001

static const uint8_t TX = 16;
static const uint8_t RX = 17;

static const int8_t SDA = -1;
static const int8_t SCL = -1;

static const uint8_t MISO = 20;
static const uint8_t SCK = 21;
static const uint8_t MOSI = 22;
static const uint8_t SS = 23;

#endif /* Pins_Arduino_h */
