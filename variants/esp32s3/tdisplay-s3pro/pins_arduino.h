#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// used for keyboard, battery gauge, charger and haptic driver
static const uint8_t SDA = 5;
static const uint8_t SCL = 6;

// Default SPI will be mapped to Radio
static const uint8_t SS = 7;
static const uint8_t MOSI = 17;
static const uint8_t MISO = 8;
static const uint8_t SCK = 18;

#endif /* Pins_Arduino_h */
