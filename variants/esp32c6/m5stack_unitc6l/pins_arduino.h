#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886
#define USB_PID 0x0048

static const uint8_t TX = 16;
static const uint8_t RX = 17;

static const uint8_t SDA = 10;
static const uint8_t SCL = 8;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 22;
static const uint8_t SCK = 20;
static const uint8_t MOSI = 21;
static const uint8_t SS = 6;

// #define SPI_MOSI (11)
// #define SPI_SCK (14)
// #define SPI_MISO (2)
// #define SPI_CS (13)

// #define SDCARD_CS SPI_CS

#endif /* Pins_Arduino_h */
