#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x2886 // Seeed Technology Co., Ltd
#define USB_PID 0x0059

// map SPI
static const uint8_t SS = LORA_CS;
static const uint8_t SCK = LORA_SCK;
static const uint8_t MOSI = LORA_MOSI;
static const uint8_t MISO = LORA_MISO;

// map I2C
static const uint8_t SCL = 48;
static const uint8_t SDA = 47;

#endif /* Pins_Arduino_h */
