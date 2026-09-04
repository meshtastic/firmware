#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t SS = 13;
static const uint8_t MOSI = 21;
static const uint8_t MISO = 47;
static const uint8_t SCK = 14;

static const uint8_t SCL = I2C_SCL;
static const uint8_t SDA = I2C_SDA;

#endif
