#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// used for keyboard, touch controller, beam sensor, and gyroscope
static const uint8_t SDA = 13;
static const uint8_t SCL = 14;

// Default SPI will be mapped to Radio
static const uint8_t SS = 3;
static const uint8_t MOSI = 33;
static const uint8_t MISO = 47;
static const uint8_t SCK = 36;

#endif /* Pins_Arduino_h */
