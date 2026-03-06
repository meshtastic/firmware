#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 18;
static const uint8_t SCL = 9;

// Default SPI will be mapped to Radio
static const uint8_t SS = -1;
static const uint8_t MOSI = 17;
static const uint8_t MISO = 6;
static const uint8_t SCK = 8;

#define SPI_MOSI (39)
#define SPI_SCK (41)
#define SPI_MISO (38)
#define SPI_CS (40)

#define SDCARD_CS SPI_CS

#endif /* Pins_Arduino_h */
