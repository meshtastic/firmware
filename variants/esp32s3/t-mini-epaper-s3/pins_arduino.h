#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x1001

// Default Wire
static const uint8_t SDA = 18;
static const uint8_t SCL = 9;

// Default SPI (LoRa bus)
static const uint8_t SS = 7;
static const uint8_t MOSI = 17;
static const uint8_t MISO = 6;
static const uint8_t SCK = 8;

// SD card SPI bus
#define SPI_MOSI (39)
#define SPI_SCK (41)
#define SPI_MISO (38)
#define SPI_CS (40)

#endif /* Pins_Arduino_h */
