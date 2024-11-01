#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886
#define USB_PID 0x0059

// GPIO48 Reference: https://github.com/espressif/arduino-esp32/pull/8600

// The default Wire will be mapped to Screen and Sensors
static const uint8_t SDA = 8;
static const uint8_t SCL = 9;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 39;
static const uint8_t SCK = 21;
static const uint8_t MOSI = 38;
static const uint8_t SS = 17;

#endif /* Pins_Arduino_h */
