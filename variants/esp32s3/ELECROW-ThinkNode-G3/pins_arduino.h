#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 17;
static const uint8_t SCL = 18;

// Default SPI will be mapped to Radio
static const uint8_t SS = 39;
static const uint8_t MOSI = 40;
static const uint8_t MISO = 41;
static const uint8_t SCK = 42;

// #define SPI_MOSI (11)
// #define SPI_SCK (10)
// #define SPI_MISO (9)
// #define SPI_CS (12)

// #define SDCARD_CS SPI_CS

#endif /* Pins_Arduino_h */