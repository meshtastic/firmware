#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to RTC and Touch
static const uint8_t SDA = 39;
static const uint8_t SCL = 40;

// Default SPI will be mapped to Radio
static const uint8_t SS = 46;
static const uint8_t MOSI = 13;
static const uint8_t MISO = 21;
static const uint8_t SCK = 14;

#define SPI_MOSI (13)
#define SPI_SCK (14)
#define SPI_MISO (21)
#define SPI_CS (12)

#endif /* Pins_Arduino_h */
