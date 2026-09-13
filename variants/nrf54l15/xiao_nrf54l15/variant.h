#pragma once

/*
 * Seeed XIAO nRF54L15 with a Wio-SX1262 for XIAO (SKU 113010003) on the header.
 *
 * This header shadows the framework's variants/xiao_nrf54l15/variant.h, so it carries the core
 * pin table definitions as well. Arduino pins 0..10 are the XIAO header D0..D10, 11..23 are
 * internal signals (see variant.cpp for the physical GPIO of each index).
 */

#define VARIANT_MCK (128000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (24)
#define NUM_DIGITAL_PINS (24)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)
#define ADC_RESOLUTION 14

#define D0 (0ul)
#define D1 (1ul)
#define D2 (2ul)
#define D3 (3ul)
#define D4 (4ul)
#define D5 (5ul)
#define D6 (6ul)
#define D7 (7ul)
#define D8 (8ul)
#define D9 (9ul)
#define D10 (10ul)

#define PIN_A0 D0
#define PIN_A1 D1
#define PIN_A2 D2
#define PIN_A3 D3
#define PIN_A4 D4
#define PIN_A5 D5
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;

// User LED P2.00 (active low)
#define PIN_LED1 16
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 0

// User button P0.00 (active low)
#define PIN_BUTTON1 17
#define BUTTON_NEED_PULLUP

// Serial1: the SAMD11 USB-CDC bridge (UARTE20): nRF TX P1.09, nRF RX P1.08
#define PIN_SERIAL1_TX 18
#define PIN_SERIAL1_RX 19
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

// Serial2: header D6 (TX) / D7 (RX) on UARTE21
#define PIN_SERIAL2_TX D6
#define PIN_SERIAL2_RX D7
#define SERIAL2_UARTE NRF_UARTE21
#define SERIAL2_IRQN SERIAL21_IRQn
#define SERIAL2_IRQ_HANDLER SERIAL21_IRQHandler

// SPI (SPIM00): D8 SCK, D9 MISO, D10 MOSI
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO D9
#define PIN_SPI_MOSI D10
#define PIN_SPI_SCK D8
static const uint8_t SS = D4;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// Wire (TWIM30): the Sense variant's internal sensor bus, SDA P0.04, SCL P0.03
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 12
#define PIN_WIRE_SCL 11
#define WIRE_TWIM NRF_TWIM30
#define WIRE_TWIS NRF_TWIS30
#define WIRE_IRQN SERIAL30_IRQn
#define WIRE_IRQ_HANDLER SERIAL30_IRQHandler

#ifdef __cplusplus
}
#endif

// Wio-SX1262 for XIAO
#define USE_SX1262
#define SX126X_CS D4
#define SX126X_DIO1 D1
#define SX126X_BUSY D3
#define SX126X_RESET D2
#define SX126X_RXEN D5
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
