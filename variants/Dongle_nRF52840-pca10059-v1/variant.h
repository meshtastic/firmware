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

#ifndef _VARIANT_NORDIC_PCA10059_
#define _VARIANT_NORDIC_PCA10059_

#define PCA10059

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

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
#define PIN_LED1 (0 + 6)      // Built in Green   P0.06
#define PIN_LED2 (0 + 6)      // Just here for completeness
#define RGBLED_RED (0 + 8)    // Red of RGB     P0.08
#define RGBLED_GREEN (32 + 9) // Green of RGB  P1.09
#define RGBLED_BLUE (0 + 12)  // Blue of RGB   P0.12
#define RGBLED_CA             // comment out this line if you have a common cathode type, as defined use common anode logic

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 0 // State when LED is litted

/*
 * Buttons
 */

#define PIN_BUTTON1 (32 + 6) // BTN_DN           P1.06 Built in button

/*
 * Analog pins
 */
#define PIN_A0 (-1)

static const uint8_t A0 = PIN_A0;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (-1) // AREF            Not yet used

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (0 + 17) // MISO      P0.17
#define PIN_SPI_MOSI (0 + 15) // MOSI      P0.15
#define PIN_SPI_SCK (0 + 13)  // SCK       P0.13

#define PIN_SPI1_MISO (-1) //
#define PIN_SPI1_MOSI (10) // EPD_MOSI  P0.10
#define PIN_SPI1_SCK (9)   // EPD_SCLK  P0.09

static const uint8_t SS = (0 + 31); // LORA_CS   P0.31
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */

// #define PIN_EINK_EN  (-1)
#define PIN_EINK_EN (0 + 6) // Turn on the Green built in LED
#define PIN_EINK_CS (32)    // EPD_CS
#define PIN_EINK_BUSY (20)  // EPD_BUSY
#define PIN_EINK_DC (24)    // EPD_D/C
#define PIN_EINK_RES (-1)   // Not Connected P0.22 available
#define PIN_EINK_SCLK (9)   // EPD_SCLK
#define PIN_EINK_MOSI (10)  // EPD_MOSI

#define USE_EINK

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4) // SDA
#define PIN_WIRE_SCL (32 + 7) // SCL

// NiceRF 868 LoRa module
#define USE_SX1262
#define SX126X_CS (0 + 31)     // LORA_CS     P0.31
#define SX126X_DIO1 (0 + 29)   // DIO1        P0.29
#define SX126X_BUSY (0 + 2)    // LORA_BUSY   P0.02
#define SX126X_RESET (32 + 15) // LORA_RESET  P1.15
#define SX126X_TXEN (32 + 13)  // TXEN        P1.13 NiceRF 868 dont use
#define SX126X_RXEN (32 + 10)  // RXEN        P1.10 NiceRF 868 dont use

#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_GPS_EN (-1)
#define PIN_GPS_PPS (-1) // Pulse per second input from the GPS

#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (1.73F)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
