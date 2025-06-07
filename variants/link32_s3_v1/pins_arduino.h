#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

// Default SPI will be mapped to Radio
static const uint8_t SS = 21;
static const uint8_t MOSI = 34;
static const uint8_t MISO = 33;
static const uint8_t SCK = 16;

#endif /* Pins_Arduino_h */
