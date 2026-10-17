#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886
#define USB_PID 0x0059

// I2C Configuration on alternate pins
static const uint8_t SDA = 16;
static const uint8_t SCL = 17;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 20;
static const uint8_t SCK = 19;
static const uint8_t MOSI = 18;
static const uint8_t SS = 22;

#endif /* Pins_Arduino_h */
