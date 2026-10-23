#pragma once

/*
 * Nordic nRF54LM20-DK (PCA10184) with an EBYTE E22-900M30S (SX1262).
 *
 * The DK carries an nRF54LM20B; the image is built for the nRF54LM20A (same die without the Axon NPU).
 *
 * This header shadows the framework's variants/nrf54lm20dk/variant.h, so it carries the core
 * pin table definitions as well. Arduino pin = physical GPIO: P0.n = n, P1.n = 32+n, P2.n = 64+n,
 * P3.n = 96+n.
 *
 * Serial peripherals are port bound: SERIAL00 (UARTE00/SPIM00) -> P2, SERIAL2x -> P1 and P3,
 * SERIAL30 -> P0. Pin interrupts: P0 -> GPIOTE30, P1 and P3 -> GPIOTE20, P2 none.
 *
 * VDD: the DK ships at 1.8 V; the nPM1300 sets VDD:nRF anywhere from 1.8 to 3.3 V (Board Configurator)
 * and VDD:IO follows it. The E22's SPI and DIO lines are 3.3 V, so raise VDD to 3.3 V before the module
 * is connected. Nordic advises 2.7 V at most for the on-board LEDs (transistor-buffered, fed by the PMIC);
 * they are status only here, so they may go dark at 3.3 V without harm to the node.
 *
 * Free header pins on this DK: P0.03, P0.04, P1.03-P1.07, P1.13, P3.00-P3.06. P2.00-P2.05 go to the
 * MX25R64 by default (board-controller switches), so the nRF54L15-DK harness on P2 does not carry over.
 * E22 wiring: SPI and control on P3 (SPIM22; DIO1 on GPIOTE20), RXEN on P1 (held high, any GPIO will do):
 *   MOSI P3.00, MISO P3.01, NSS P3.02, SCK P3.03, BUSY P3.04, DIO1 P3.05, NRESET P3.06, RXEN P1.13
 *   DIO2 -> TXEN bridge on the module, DIO3 drives the TCXO (1.8 V).
 *
 * SPIM SCK and TWIM SCL must sit on clock pins (datasheet v1.0, 10.1.2): P3.03 and P0.04 are, as are
 * P0.03, P1.03, P1.04, P1.07 and P1.13 among the free header pins. P1 and P3 run at 8 MHz at most.
 */

#define VARIANT_MCK (128000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (109)
#define NUM_DIGITAL_PINS (109)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)
#define ADC_RESOLUTION 14

// LEDs (active HIGH on this DK): LED0 P1.22 status, LED1 P1.25
#define PIN_LED1 54
#define PIN_LED2 57
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 1

// BTN0 P1.26 (active low)
#define PIN_BUTTON1 58
#define BUTTON_NEED_PULLUP

// Serial1: VCOM serial port 1 of the on-board J-Link (UARTE20): TX P1.16, RX P1.17
#define PIN_SERIAL1_RX 49
#define PIN_SERIAL1_TX 48
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

// Serial2 (UARTE21, serial module): RX P1.06, TX P1.07; only P1/P3 pins can be assigned to it.
// Leaves P1.03-P1.05 (AIN7-AIN5) free for analog use.
#define PIN_SERIAL2_RX 38
#define PIN_SERIAL2_TX 39
#define SERIAL2_UARTE NRF_UARTE21
#define SERIAL2_IRQN SERIAL21_IRQn
#define SERIAL2_IRQ_HANDLER SERIAL21_IRQHandler

// SPI (SPIM22, header P5) for the E22: SERIAL22 reaches P3; 8 MHz at most (16 MHz core clock)
#define SPI_INTERFACES_COUNT 1
#define SPI_SPIM NRF_SPIM22
#define PIN_SPI_MISO 97
#define PIN_SPI_MOSI 96
#define PIN_SPI_SCK 99
static const uint8_t SS = 98;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// I2C (TWIM30, header P1): SDA P0.03, SCL P0.04, external 4.7k pull-ups required.
// SERIAL30 is also VCOM serial port 0 (P0.06-P0.09), which is therefore not used.
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
#define SX126X_CS 98     // P3.02
#define SX126X_DIO1 101  // P3.05 (GPIOTE20)
#define SX126X_BUSY 100  // P3.04
#define SX126X_RESET 102 // P3.06
// RXEN is held high permanently (LNA always on); TXEN follows DIO2.
#define SX126X_ANT_SW 45 // P1.13
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f
