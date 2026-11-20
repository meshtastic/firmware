#pragma once

/*
 * Seeed XIAO nRF54L15 with a Wio-SX1262 for XIAO (SKU 113010003), or with XIAO_NRF54L15_LR2021 the
 * Wio-LR2021 on the LoRa Plus expansion board (SKU 100039980, SSD1306 OLED and Grove I2C on D4/D5).
 * The two modules share D1..D3 with different roles, so they are separate build environments.
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
#ifdef XIAO_NRF54L15_LR2021
// K1 on the LoRa Plus expansion board, D14 (P2.09)
#define PIN_BUTTON2 14
#endif

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
#ifdef XIAO_NRF54L15_LR2021
static const uint8_t SS = D3;
#else
static const uint8_t SS = D4;
#endif
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#ifdef XIAO_NRF54L15_LR2021
// Wire (TWIM22): header I2C on D4 (SDA, P1.10) / D5 (SCL, P1.11)
#define PIN_WIRE_SDA D4
#define PIN_WIRE_SCL D5
#define WIRE_TWIM NRF_TWIM22
#define WIRE_TWIS NRF_TWIS22
#define WIRE_IRQN SERIAL22_IRQn
#define WIRE_IRQ_HANDLER SERIAL22_IRQHandler
#else
// Wire (TWIM30): the Sense variant's internal sensor bus, SDA P0.04, SCL P0.03; D4/D5 belong to the radio
#define PIN_WIRE_SDA 12
#define PIN_WIRE_SCL 11
#define WIRE_TWIM NRF_TWIM30
#define WIRE_TWIS NRF_TWIS30
#define WIRE_IRQN SERIAL30_IRQn
#define WIRE_IRQ_HANDLER SERIAL30_IRQHandler
#endif
#define WIRE_INTERFACES_COUNT 1

#ifdef __cplusplus
}
#endif

#ifdef XIAO_NRF54L15_LR2021
// Wio-LR2021: NSS D3, IRQ on DIO8 D0, NRESET D2, BUSY D1, switchless RF, 32 MHz crystal (no TCXO),
// DIO7/DIO11 reach D6/D7 through 470R and stay unused
#define USE_LR2021
#define LR2021_SPI_NSS_PIN D3
#define LR2021_IRQ_PIN D0
#define LR2021_NRESET_PIN D2
#define LR2021_BUSY_PIN D1
#define LR2021_SPI_SCK_PIN PIN_SPI_SCK
#define LR2021_SPI_MOSI_PIN PIN_SPI_MOSI
#define LR2021_SPI_MISO_PIN PIN_SPI_MISO
#define IRQ_DIO_NUM 8
#else
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
#endif
