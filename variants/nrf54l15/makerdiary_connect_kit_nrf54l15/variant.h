#pragma once

/*
 * Nordic nRF54L15-DK customized for MakerDiary Connect Kit + Core1121-HF + InkHUD
 * Arduino pin = physical GPIO: P0.n = n, P1.n = 32+n, P2.n = 64+n.
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

// --- LED ---
#define PIN_LED1            2 // P0.02
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 1
#define PIN_LED2 -1

#define PIN_BUTTON1         45 // P1.13
#define BUTTON_NEED_PULLUP

// --- Serial1 ---
#define PIN_SERIAL1_RX      37 // P1.05
#define PIN_SERIAL1_TX      38 // P1.06
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

#define PIN_BUZZER          36 // P1.04

// --- Serial2 ---
#define PIN_SERIAL2_RX      47 // P1.15
#define PIN_SERIAL2_TX      48 // P1.16
#define SERIAL2_UARTE NRF_UARTE21
#define SERIAL2_IRQN SERIAL21_IRQn
#define SERIAL2_IRQ_HANDLER SERIAL21_IRQHandler

#define SPI_INTERFACES_COUNT 2

// --- SPI for Core1121-HF ---
#define PIN_SPI_MISO        68  // P2.04
#define PIN_SPI_MOSI        66  // P2.02
#define PIN_SPI_SCK         65  // P2.01
static const uint8_t SS =   69; // P2.05 = radio CS/NSS
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// --- SPI for E-Ink ---
#define PIN_SPI1_MISO       -1 // Not used
#define PIN_SPI1_MOSI       39 // P1.7
#define PIN_SPI1_SCK        43 // P1.11

#define PIN_EINK_MOSI PIN_SPI1_MOSI
#define PIN_EINK_SCLK PIN_SPI1_SCK

#define PIN_EINK_CS         71 // P2.07
#define PIN_EINK_DC         72 // P2.08
#define PIN_EINK_RES        73 // P2.09
#define PIN_EINK_BUSY       74 // P2.10

// --- I2C ---
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA        3 // P0.03
#define PIN_WIRE_SCL        4 // P0.04
#define WIRE_TWIM NRF_TWIM30
#define WIRE_TWIS NRF_TWIS30
#define WIRE_IRQN SERIAL30_IRQn
#define WIRE_IRQ_HANDLER SERIAL30_IRQHandler

#ifdef __cplusplus
}
#endif

// --- Core1121 ---

#undef USE_SX1262
#define USE_LR1121
#ifdef USE_LR1121

#define LR1121_NRESET_PIN   64   // P2.00
#define LR1121_BUSY_PIN     67   // P2.03
#define LR1121_SPI_NSS_PIN  69   // P2.05
#define LR1121_IRQ_PIN      70   // P2.06

#define LR1121_SPI_SCK_PIN  PIN_SPI_SCK
#define LR1121_SPI_MOSI_PIN PIN_SPI_MOSI
#define LR1121_SPI_MISO_PIN PIN_SPI_MISO

#define LR11X0_DIO_AS_RF_SWITCH
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8f

#endif
