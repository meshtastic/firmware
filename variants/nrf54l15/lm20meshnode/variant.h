#pragma once

/*
 * LM20MeshNode rev 0.1, open hardware pocket node: https://github.com/cvaldess/LM20MeshNode
 *   Minew ME54BS13 module (nRF54LM20A, PCB antenna, 32.768 kHz crystal), USB-C to the nRF54LM20 USB port
 *   EBYTE E22-900MM22S (SX1262, crystal, up to 22 dBm), RXEN from the MCU, TXEN from DIO2
 *   EBYTE E108-GN04S GNSS (u-blox compatible, 38400 bps) on a rail the nPM1300 switches
 *   nPM1300 PMIC: 1S LiPo charger, 3.3 V main rail (BUCK2), GNSS rail (BUCK1)
 *   0.91" 128x32 SSD1306 OLED, three side buttons, buzzer, status LED, SHT41, LSM6DS3TR-C
 *
 * This header shadows the framework's variants/lm20meshnode/variant.h, so it carries the core pin table
 * definitions as well. Arduino pin = physical GPIO: P0.n = n, P1.n = 32+n, P2.n = 64+n, P3.n = 96+n.
 *
 * Serial peripherals are port bound: SERIAL00 (UARTE00/SPIM00) -> P2, SERIAL2x -> P1 and P3,
 * SERIAL30 -> P0. Pin interrupts: P0 -> GPIOTE30, P1 and P3 -> GPIOTE20, P2 none.
 * SPIM SCK and TWIM SCL sit on clock pins (P1.17, P0.04); P1 runs SPI at 8 MHz at most.
 *
 * The board has not been built yet: this variant follows the schematic (rev 0.1) and is untested.
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

// Status LED P2.00 (green, active high). The nPM1300 drives the error (D102, blue) and charge (D103, green)
// LEDs itself, in their reset modes.
#define PIN_LED1 64
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 1

// Serial1: the E108-GN04S (UARTE20), 1 kOhm in series: nRF TX P1.03 -> GNSS RXD, GNSS TXD -> nRF RX P1.29
#define PIN_SERIAL1_RX 61
#define PIN_SERIAL1_TX 35
#define SERIAL1_UARTE NRF_UARTE20
#define SERIAL1_IRQN SERIAL20_IRQn
#define SERIAL1_IRQ_HANDLER SERIAL20_IRQHandler

// SPI (SPIM22) for the E22: MISO P1.15, MOSI P1.16, SCK P1.17, NSS P1.18
#define SPI_INTERFACES_COUNT 1
#define SPI_SPIM NRF_SPIM22
#define PIN_SPI_MISO 47
#define PIN_SPI_MOSI 48
#define PIN_SPI_SCK 49
static const uint8_t SS = 50;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// I2C (TWIM30): SDA P0.03, SCL P0.04, 4.7 kOhm pull-ups on the board.
// nPM1300 0x6B, LSM6DS3TR-C 0x6A, SHT41 0x44, OLED 0x3C, and the Qwiic-style connector J501.
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

// No second UART for the serial module: Serial1 is the GNSS and Serial is USB CDC (as on the ThinkNode M8).
// The debug UART test pads (UARTE00: TX P2.02, RX P2.07) are left to the bootloader and to debugging.
#define SERIAL_PRINT_PORT 0

// SX1262 / E22-900MM22S
#define USE_SX1262
#define SX126X_CS 50    // P1.18
#define SX126X_DIO1 55  // P1.23 (GPIOTE20)
#define SX126X_BUSY 51  // P1.19
#define SX126X_RESET 58 // P1.26
#define SX126X_RXEN 45  // P1.13 (100 kOhm pull-down on the board)
#define SX126X_TXEN RADIOLIB_NC
// TXEN follows DIO2 through R301 (0 Ohm). P1.12 reaches TXEN only through R302, which is not fitted.
#define SX126X_DIO2_AS_RF_SWITCH
// The E22-900MM22S has a crystal, not a TCXO: no SX126X_DIO3_TCXO_VOLTAGE.

// GNSS: E108-GN04S on Serial1
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX
#define GPS_BAUDRATE 38400
#define PIN_GPS_PPS 36   // P1.04
#define PIN_GPS_RESET 32 // P1.00 -> RESET_N (internal pull-up in the module)
#define GPS_RESET_MODE LOW
// P1.05 drives the nPM1300's GPIO0, which lateInitVariant() makes the BUCK1 (GNSS rail) enable.
// The backup supply (V_BCKP) stays on from 3.3 V, so the receiver keeps its almanac while the rail is off.
#define PIN_GPS_EN 37
#define GPS_EN_ACTIVE HIGH

// Side buttons to GND, internal pull-ups: UP P1.27, DOWN P1.25, OK P1.28
#define INPUTDRIVER_ENCODER_TYPE 2
#define INPUTDRIVER_ENCODER_UP 59
#define INPUTDRIVER_ENCODER_DOWN 57
#define INPUTDRIVER_ENCODER_BTN 60
#define UPDOWN_LONG_PRESS_REPEAT_INTERVAL 150

// Magnetic buzzer through a BSS138 (P1.09 high = on)
#define PIN_BUZZER 41

// Battery: VBAT through 1 MOhm / 1 MOhm (R103/R104) to P1.30 (AIN2)
#define BATTERY_PIN 62
#define ADC_MULTIPLIER 2.0

// 0.91" SSD1306 on the back of the board
#define OLED_GEOMETRY_OVERRIDE GEOMETRY_128_32

// nPM1300 battery charge current, set by lateInitVariant(): 32 to 800 mA in 2 mA steps.
// 200 mA suits 1S LiPo cells from 200 mAh up at 1C or less; the VBUS input is limited to 500 mA.
#ifndef NPM1300_CHARGE_CURRENT_MA
#define NPM1300_CHARGE_CURRENT_MA 200
#endif
