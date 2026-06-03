#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

static const uint8_t TX = UART_TX;
static const uint8_t RX = UART_RX;

static const uint8_t SDA = I2C_SDA;
static const uint8_t SCL = I2C_SCL;

static const uint8_t SS = 8;
static const uint8_t MOSI = 7;
static const uint8_t MISO = 6;
static const uint8_t SCK = 10;

static const uint8_t A0 = 0;
static const uint8_t A1 = 1;
static const uint8_t A2 = 2;
static const uint8_t A3 = 3;
static const uint8_t A4 = 4;
static const uint8_t A5 = 5;

#endif /* Pins_Arduino_h */
