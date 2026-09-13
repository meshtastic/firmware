#pragma once

/*
 * Nordic nRF54L15-DK (PCA10156) with an EBYTE E22-900M30S (SX1262) on the J2 header.
 *
 * This header shadows the framework's variants/nrf54l15dk/variant.h, so it carries the core
 * pin table definitions as well. Arduino pin = physical GPIO: P0.n = n, P1.n = 32+n, P2.n = 64+n.
 *
 * GPIO supply domains: P0 3.0 V, P1 1.8 V (too low for the SX1262), P2 3.0 V.
 * Serial peripherals are port bound: SERIAL00 (UARTE00/SPIM00) -> P2, SERIAL2x -> P1, SERIAL30 -> P0.
 *
 * E22 wiring (all P2, SPIM00):
 *   SCK P2.01, MOSI P2.02, BUSY P2.03, MISO P2.04, NSS P2.05, DIO1 P2.06, RXEN P2.07, NRESET P2.00
 *   DIO2 -> TXEN bridge on the module, DIO3 drives the TCXO (1.8 V).
 */

#define VARIANT_MCK (128000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (96)
#define NUM_DIGITAL_PINS (96)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)
#define ADC_RESOLUTION 14

// LEDs (active low): LED1 P1.10 status, LED0 P2.09
#define PIN_LED1 42
#define PIN_LED2 73
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 0

// BTN0 P1.13 (active low)
#define PIN_BUTTON1 45
#define BUTTON_NEED_PULLUP

// Serial1: VCOM0 of the on-board J-Link (UARTE20): TX P1.04, RX P1.05
#define PIN_SERIAL1_RX 37
#define PIN_SERIAL1_TX 36
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

// Serial2 (UARTE21, serial module): RX P1.15, TX P1.16; only P1 pins can be assigned to it
#define PIN_SERIAL2_RX 47
#define PIN_SERIAL2_TX 48
#define SERIAL2_UARTE NRF_UARTE21
#define SERIAL2_IRQN SERIAL21_IRQn
#define SERIAL2_IRQ_HANDLER SERIAL21_IRQHandler

// SPI (SPIM00) for the E22
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO 68
#define PIN_SPI_MOSI 66
#define PIN_SPI_SCK 65
static const uint8_t SS = 69;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// I2C (TWIM30): SDA P0.03, SCL P0.04, external 4.7k pull-ups required
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 3
#define PIN_WIRE_SCL 4
#define WIRE_TWIM NRF_TWIM30
#define WIRE_TWIS NRF_TWIS30
#define WIRE_IRQN SERIAL30_IRQn
#define WIRE_IRQ_HANDLER SERIAL30_IRQHandler

#ifdef __cplusplus
}
#endif

// SX1262 / E22-900M30S
#define USE_SX1262
#define SX126X_CS 69
#define SX126X_DIO1 70
#define SX126X_BUSY 67
#define SX126X_RESET 64
// RXEN is held high permanently (LNA always on); TXEN follows DIO2.
#define SX126X_ANT_SW 71
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f
