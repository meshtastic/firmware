#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// GPIO48 Reference: https://github.com/espressif/arduino-esp32/pull/8600

// The default Wire will be mapped to Screen and Sensors
static const uint8_t SDA = 5;
static const uint8_t SCL = 6;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 14;
static const uint8_t SCK = 12;
static const uint8_t MOSI = 13;
static const uint8_t SS = 11;

#endif /* Pins_Arduino_h */