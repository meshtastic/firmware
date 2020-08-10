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

#pragma once

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// This board does not have a 32khz crystal
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
#define PINS_COUNT (46)
#define NUM_DIGITAL_PINS (46)
#define NUM_ANALOG_INPUTS (0)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (0)
#define PIN_LED2 (1)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_RED PIN_LED1
#define LED_GREEN PIN_LED2

// FIXME, bluefruit automatically blinks this led while connected.  call AdafruitBluefruit::autoConnLed to change this.
#define LED_BLUE LED_GREEN

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1 4 // center
#define PIN_BUTTON2 2
#define PIN_BUTTON3 3
#define PIN_BUTTON4 5
#define PIN_BUTTON5 6

/*
 * Analog pins
 */
#define PIN_A0 (0xff)
#define PIN_A1 (0xff)
#define PIN_A2 (0xff)
#define PIN_A3 (0xff)
#define PIN_A4 (0xff)
#define PIN_A5 (0xff)
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
#define PIN_AREF (0xff)
//#define PIN_NFC1 (9)
//#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */

// GPS is on Serial1
#define PIN_SERIAL1_RX (8)
#define PIN_SERIAL1_TX (9)

// Connected to Jlink CDC
//#define PIN_SERIAL2_RX (8)
//#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (15)
#define PIN_SPI_MOSI (13)
#define PIN_SPI_SCK (12)

// static const uint8_t SS = 44;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 2)
#define PIN_WIRE_SCL (32)

#define GPS_I2C_ADDR FIXME

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/
