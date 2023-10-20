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

// This board does have a 32khz crystal
#define USE_LFXO // Board uses 32khz crystal for LF
// #define USE_LFRC // Board uses RC for LF

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
#define PIN_LED1 (25)
#define PIN_LED2 (11)

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
#define PIN_BUTTON1 4 // up
#define PIN_BUTTON2 2 // left
#define PIN_BUTTON3 3 // center
#define PIN_BUTTON4 5 // right
#define PIN_BUTTON5 6 // down

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
// #define PIN_NFC1 (9)
// #define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */

// GPS is on Serial1
#define PIN_SERIAL1_RX (8)
#define PIN_SERIAL1_TX (9)

// We intentionally leave this undefined so we don't even try to make a Ublox driver
// #define GPS_TX_PIN PIN_SERIAL1_TX
// #define GPS_RX_PIN PIN_SERIAL1_RX

#define PIN_GPS_RESET 29 // active high
#define PIN_GPS_PPS 28
// #define PIN_GPS_WAKE 20 // CELL_CTRL in schematic? based on their example code
#define PIN_GPS_EN 7 // GPS_EN active high

// #define PIN_VUSB_EN 21

// LCD

#define PIN_LCD_RESET 23 // active low, pulse low for 20ms at boot
#define USE_ST7567

/// Charge controller I2C address
#define BQ25703A_ADDR 0x6b

// Define if screen should be mirrored left to right
#define SCREEN_MIRROR

// LCD screens are slow, so slowdown the wipe so it looks better
#define SCREEN_TRANSITION_FRAMERATE 10 // fps

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

// CUSTOM GPIOs the SX1262
#define USE_SX1262
#define SX126X_CS (0 + 10) // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 (0 + 20)
#define SX1262_DIO2 (0 + 26)
#define SX126X_BUSY (0 + 19)
#define SX126X_RESET (0 + 17)
#define SX126X_TXEN (0 + 24)
#define SX126X_RXEN (0 + 22)
// Not really an E22 but this board clones using DIO3 for tcxo control
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// FIXME, to prevent burning out parts I've set the power level super low, because I don't have
// an antenna wired up
#define SX126X_MAX_POWER 1

#define LORA_DISABLE_SENDING // Define this to disable transmission for testing (power testing etc...)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/
