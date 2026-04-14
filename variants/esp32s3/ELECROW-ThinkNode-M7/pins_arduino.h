#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 17;
static const uint8_t SCL = 18;

// Default SPI is the LR1110 radio bus
static const uint8_t SS = 12;
static const uint8_t MOSI = 10;
static const uint8_t MISO = 9;
static const uint8_t SCK = 11;

#endif /* Pins_Arduino_h */
