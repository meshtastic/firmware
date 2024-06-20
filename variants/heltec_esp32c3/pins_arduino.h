#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

// Serial
static const uint8_t TX = UART_TX;
static const uint8_t RX = UART_RX;

// Default SPI will be mapped to Radio
static const uint8_t SS = LORA_CS;
static const uint8_t SCK = LORA_SCK;
static const uint8_t MOSI = LORA_MOSI;
static const uint8_t MISO = LORA_MISO;

// Wire
static const uint8_t SCL = I2C_SCL;
static const uint8_t SDA = I2C_SDA;

#endif /* Pins_Arduino_h */
