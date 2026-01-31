#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "variant.h"
#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 9;
static const uint8_t SCL = 40;

// Default SPI will be mapped to Radio
static const uint8_t SS = 12;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 10;
static const uint8_t SCK = 13;

#define SPI_INTERFACES_COUNT 1

#define SPI_MOSI (11)
#define SPI_SCK (13)
#define SPI_MISO (10)
#define SPI_CS (12)

#ifdef _VARIANT_RAK3112_
/*
 * Serial interfaces
 */
// TXD1 RXD1 on Base Board
#define PIN_SERIAL1_RX (44)
#define PIN_SERIAL1_TX (43)

/*
 * Internal SPI to LoRa transceiver
 */
#define LORA_SX126X_SCK 5
#define LORA_SX126X_MISO 3
#define LORA_SX126X_MOSI 6

/*
 * Analog pins
 */
#define PIN_A0 (21)
#define PIN_A1 (14)

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE1_SDA (17)
#define PIN_WIRE1_SCL (18)

/*
 * GPIO's
 */
#define WB_IO1 21
#define WB_IO2 2
// #define WB_IO2 14
#define WB_IO3 41
#define WB_IO4 42
#define WB_IO5 38
#define WB_IO6 39
// #define WB_SW1 35 NC
#define WB_A0 1
#define WB_A1 2
#define WB_CS 12
#define WB_LED1 46
#define WB_LED2 45
#endif

#endif /* Pins_Arduino_h */
