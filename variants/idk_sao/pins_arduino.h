#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 5;
static const uint8_t SCL = 6;

// Default SPI will be mapped to Radio
static const uint8_t SS = LORA_CS;
static const uint8_t SCK = LORA_SCK;
static const uint8_t MOSI = LORA_MOSI;
static const uint8_t MISO = LORA_MISO;

// #define SDCARD_CS                   SPI_CS

#endif /* Pins_Arduino_h */