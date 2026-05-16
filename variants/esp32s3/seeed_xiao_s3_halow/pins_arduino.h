#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886
#define USB_PID 0x0059

// I2C is unused on this variant (HaLow BUSY claims GPIO 5), but the Arduino
// framework's default Wire still needs valid SDA/SCL values to compile.
static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

// Default SPI is shared with the HaLow module. SS = HaLow CS.
static const uint8_t MISO = 8;
static const uint8_t SCK = 7;
static const uint8_t MOSI = 9;
static const uint8_t SS = 4;

#endif /* Pins_Arduino_h */
