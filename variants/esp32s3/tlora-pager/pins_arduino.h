#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// used for keyboard, battery gauge, charger and haptic driver
static const uint8_t SDA = 3;
static const uint8_t SCL = 2;

// Default SPI will be mapped to Radio
static const uint8_t SS = 36;
static const uint8_t MOSI = 34;
static const uint8_t MISO = 33;
static const uint8_t SCK = 35;

#endif /* Pins_Arduino_h */
