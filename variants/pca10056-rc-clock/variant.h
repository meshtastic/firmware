/*
 Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
 Copyright (c) 2016 Sandeep Mistry All right reserved.
 Copyright (c) 2018, Adafruit Industries (adafruit.com)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_PCA10056_
#define _VARIANT_PCA10056_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// This file is the same as the standard pac10056 variant, except that @geeksville broke the xtal on his devboard so
// he has to use a RC clock.

// #define USE_LFXO // Board uses 32khz crystal for LF
#define USE_LFRC // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (13)
#define PIN_LED2 (14)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 0 // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1 11
#define PIN_BUTTON2 12
#define PIN_BUTTON3 24
#define PIN_BUTTON4 25

/*
 * Analog pins
 */
#define PIN_A0 (3)
#define PIN_A1 (4)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */

// Arduino Header D0, D1
#define PIN_SERIAL1_RX (33) // P1.01
#define PIN_SERIAL1_TX (34) // P1.02

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (46)
#define PIN_SPI_MOSI (45)
#define PIN_SPI_SCK (47)

static const uint8_t SS = 44;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (26)
#define PIN_WIRE_SCL (27)

// QSPI Pins
#define PIN_QSPI_SCK 19
#define PIN_QSPI_CS 17
#define PIN_QSPI_IO0 20
#define PIN_QSPI_IO1 21
#define PIN_QSPI_IO2 22
#define PIN_QSPI_IO3 23

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R6435F
#define EXTERNAL_FLASH_USE_QSPI

// CUSTOM GPIOs the SX1262MB2CAS shield when installed on the NRF52840-DK development board
#define USE_SX1262
#define SX126X_CS (32 + 8)      // P1.08
#define SX126X_DIO1 (32 + 6)    // P1.06
#define SX126X_BUSY (32 + 4)    // P1.04
#define SX126X_RESET (0 + 3)    // P0.03
#define SX126X_ANT_SW (32 + 10) // P1.10
#define SX126X_DIO2_AS_RF_SWITCH

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
