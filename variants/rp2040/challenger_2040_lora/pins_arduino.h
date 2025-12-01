#pragma once

#define PINS_COUNT (25u)
#define NUM_DIGITAL_PINS (25u)
#define NUM_ANALOG_INPUTS (4u)
#define NUM_ANALOG_OUTPUTS (0u)
#define ADC_RESOLUTION (12u)

// LEDs
#define PIN_LED (24u)

// Serial
#define PIN_SERIAL1_TX (16u)
#define PIN_SERIAL1_RX (17u)

// SPI
#define PIN_SPI0_MISO (20u)
#define PIN_SPI0_MOSI (23u)
#define PIN_SPI0_SCK (22u)
#define PIN_SPI0_SS (21u)

// Connected to LoRa module
#define PIN_SPI1_MISO (12u)
#define PIN_SPI1_MOSI (11u)
#define PIN_SPI1_SCK (10u)
#define PIN_SPI1_SS (9u)
#define RFM95W_SS (9u)
#define RFM95W_DIO0 (14u)
#define RFM95W_DIO1 (15u)
#define RFM95W_DIO2 (18u)
#define RFM95W_RST (13u)
#define RFM95W_SPI SPI1

// Wire
#define PIN_WIRE0_SDA (0u)
#define PIN_WIRE0_SCL (1u)

// Not pinned out
#define PIN_WIRE1_SDA (31u)
#define PIN_WIRE1_SCL (31u)
#define PIN_SERIAL2_RX (31u)
#define PIN_SERIAL2_TX (31u)

#define SERIAL_HOWMANY (1u)
#define SPI_HOWMANY (2u)
#define WIRE_HOWMANY (1u)

#define LED_BUILTIN PIN_LED

static const uint8_t D0 = (16u);
static const uint8_t D1 = (17u);
static const uint8_t D2 = (20u);
static const uint8_t D3 = (23u);
static const uint8_t D4 = (22u);
static const uint8_t D5 = (2u);
static const uint8_t D6 = (3u);
static const uint8_t D7 = (0u);
static const uint8_t D8 = (1u);
static const uint8_t D9 = (4u);
static const uint8_t D10 = (5u);
static const uint8_t D11 = (6u);
static const uint8_t D12 = (7u);
static const uint8_t D13 = (8u);
static const uint8_t D14 = (13u);
static const uint8_t D15 = (14u);
static const uint8_t D16 = (15u);
static const uint8_t D17 = (18u);
static const uint8_t D18 = (24u);

static const uint8_t A0 = (26u);
static const uint8_t A1 = (27u);
static const uint8_t A2 = (28u);
static const uint8_t A3 = (29u);
static const uint8_t A4 = (19u);
static const uint8_t A5 = (21u);

#ifndef SS
#define SS PIN_SPI1_SS
#endif