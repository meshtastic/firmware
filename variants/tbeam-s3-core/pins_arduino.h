#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 42;
static const uint8_t SCL = 41;

// Default SPI will be mapped to Radio
static const uint8_t SS = 10;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 13;
static const uint8_t SCK = 12;

// Another SPI bus shares SD card and QMI8653 inertial measurement sensor
#define SPI_MOSI (35)
#define SPI_SCK (36)
#define SPI_MISO (37)
#define SPI_CS (47)
#define IMU_CS (34)

#define SDCARD_CS SPI_CS
#define IMU_INT (33)
// #define PMU_IRQ                  (40)
#define RTC_INT (14)

#endif /* Pins_Arduino_h */
