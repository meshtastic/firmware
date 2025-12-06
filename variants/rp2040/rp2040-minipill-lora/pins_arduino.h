#pragma once

#define PIN_A0 (26u)
#define PIN_A1 (27u)
#define PIN_A2 (28u)
#define PIN_A3 (29u)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;

// LEDs
#define PIN_LED (23u)
#define PIN_LED1 PIN_LED
#define LED_BUILTIN PIN_LED

#define ADC_RESOLUTION 12

// Serial
#define PIN_SERIAL1_TX (16ul)
#define PIN_SERIAL1_RX (17ul)

#define PIN_SERIAL2_TX (8ul)
#define PIN_SERIAL2_RX (9ul)

// SPI
#define PIN_SPI0_MISO (4u)
#define PIN_SPI0_MOSI (3u)
#define PIN_SPI0_SCK (2u)
#define PIN_SPI0_SS (5u)

// Wire
#define PIN_WIRE0_SDA (20u)
#define PIN_WIRE0_SCL (21u)

#define PIN_WIRE1_SDA (-1)
#define PIN_WIRE1_SCL (-1)

#define SERIAL_HOWMANY (3u)
#define SPI_HOWMANY (2u)
#define WIRE_HOWMANY (1u)

static const uint8_t SS = PIN_SPI0_SS;
static const uint8_t MOSI = PIN_SPI0_MOSI;
static const uint8_t MISO = PIN_SPI0_MISO;
static const uint8_t SCK = PIN_SPI0_SCK;

static const uint8_t SDA = PIN_WIRE0_SDA;
static const uint8_t SCL = PIN_WIRE0_SCL;