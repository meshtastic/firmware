#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 7;
static const uint8_t SCL = 6;

static const uint8_t SS = 39;
static const uint8_t MOSI = 47;
static const uint8_t MISO = 38;
static const uint8_t SCK = 40;

#endif