#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 21;
static const uint8_t SCL = 15;

// Default SPI will be mapped to Radio
static const uint8_t SS = 14;
static const uint8_t MOSI = 8;
static const uint8_t MISO = 9;
static const uint8_t SCK = 3;

#define SPI_MOSI (40)
#define SPI_SCK (39)
#define SPI_MISO (13)
#define SPI_CS (10)
// IO42 TF_3V3_CTL
#define SDCARD_CS SPI_CS

#endif /* Pins_Arduino_h */
