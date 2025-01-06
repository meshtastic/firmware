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

#ifndef _VARIANT_MESHLINK_
#define _VARIANT_MESHLINK_
#ifndef MESHLINK
#define MESHLINK
#endif
/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

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
#define NUM_ANALOG_INPUTS (2)
#define NUM_ANALOG_OUTPUTS (0)

#define BUTTON_PIN (38) // If defined, this will be used for user button presses,
#define BUTTON_NEED_PULLUP

// LEDs
#define PIN_LED1 (24) // 24
#define PIN_LED2 (-1) // Watchdog enable pin25
// #define PIN_LED3 (-1)

// #define LED_RED PIN_LED1
// #define LED_GREEN LED_PIN
#define LED_BLUE PIN_LED1

// #define LED_CONN PIN_LED1
#define LED_BUILTIN PIN_LED1

#define LED_STATE_ON 0 // State when LED is litted
#define LED_INVERTED 1
// Testing USB detection
// #define NRF_APM
/*
 * Analog pins
 */
#define PIN_A0 (2)
#define PIN_A7 (31)
/*
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
*/
#define ADC_RESOLUTION 14

// Other pins
// #define PIN_AREF (2)

// static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (40)
#define PIN_SERIAL1_TX (7)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (8)
#define PIN_SPI_MOSI (32 + 9)
#define PIN_SPI_SCK (11)

#define PIN_SPI1_MISO (23)
#define PIN_SPI1_MOSI (21)
#define PIN_SPI1_SCK (19)

static const uint8_t SS = 12;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */
// #define USE_EINK

#define PIN_EINK_CS (15)
#define PIN_EINK_BUSY (16)
#define PIN_EINK_DC (14)
#define PIN_EINK_RES (17) // 17
#define PIN_EINK_SCLK (19)
#define PIN_EINK_MOSI (21) // also called SDI

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (1)
#define PIN_WIRE_SCL (27)

// QSPI Pins
#define PIN_QSPI_SCK 19
#define PIN_QSPI_CS 22
#define PIN_QSPI_IO0 21
#define PIN_QSPI_IO1 23
#define PIN_QSPI_IO2 32
#define PIN_QSPI_IO3 20
/*
// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES W25Q16JVUXIQ
#define EXTERNAL_FLASH_USE_QSPI

*/

/*
Important for successful SX1262 initialization:

* Setup DIO2 to control the antenna switch
* Setup DIO3 to control the TCXO power supply
* Setup the SX1262 to use it's DCDC regulator and not the LDO
* RAK4630 schematics show GPIO P1.07 connected to the antenna switch, but it should not be initialized, as DIO2 will do the
control of the antenna switch

SO GPIO 39/TXEN MAY NOT BE DEFINED FOR SUCCESSFUL OPERATION OF THE SX1262 - TG

*/

#define USE_SX1262
#define SX126X_CS (12)
#define SX126X_DIO1 (32 + 1)
#define SX126X_BUSY (32 + 3)
#define SX126X_RESET (6)
// #define SX126X_RXEN (13)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// enables 3.3V periphery like GPS or IO Module
// Do not toggle this for GPS power savings
#define PIN_3V3_EN (25)

// RAK1910 GPS module
// If using the wisblock GPS module and pluged into Port A on WisBlock base
// IO1 is hooked to PPS (pin 12 on header) = gpio 17
// IO2 is hooked to GPS RESET = gpio 34, but it can not be used to this because IO2 is ALSO used to control 3V3_S power (1 is on).
// Therefore must be 1 to keep peripherals powered
// Power is on the controllable 3V3_S rail
// #define PIN_GPS_RESET (34)
// #define HAS_GPS 1
// #define GNSS_ATGM336H
// #define GPS_BAUDRATE 9600
// #undef GPS_RX_PIN
// #undef GPS_TX_PIN
// #define PIN_GPS_PPS 36 // Pulse per second input from the GPS

#define GPS_TX_PIN PIN_SERIAL1_RX // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN PIN_SERIAL1_TX // This is for bits going TOWARDS the GPS

// #define GPS_THREAD_INTERVAL 50

// Define pin to enable GPS toggle (set GPIO to LOW) via user button triple press
#define PIN_GPS_EN (0)
#define GPS_EN_ACTIVE LOW

#define PIN_BUZZER PIN_A7 // IO3 is PWM2

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.42

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/
#endif