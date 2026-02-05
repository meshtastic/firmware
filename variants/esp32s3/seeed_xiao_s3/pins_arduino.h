#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886
#define USB_PID 0x0059

// GPIO48 Reference: https://github.com/espressif/arduino-esp32/pull/8600

// The default Wire will be mapped to Screen and Sensors
static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 8;
static const uint8_t SCK = 7;
static const uint8_t MOSI = 9;
static const uint8_t SS = 41;

#endif /* Pins_Arduino_h */
