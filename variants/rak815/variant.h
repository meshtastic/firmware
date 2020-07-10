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

#ifndef _VARIANT_RAK815_
#define _VARIANT_RAK815_

/** Master clock frequency */
#define VARIANT_MCK       (64000000ul)

#define USE_LFXO      // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT           (32u)
#define NUM_DIGITAL_PINS     (32u)
#define NUM_ANALOG_INPUTS    (8u)
#define NUM_ANALOG_OUTPUTS   (0u)

// LEDs
#define PIN_LED1             (25)
#define PIN_LED2             (26)

#define LED_BUILTIN          PIN_LED1
#define LED_CONN             PIN_LED2

// The BLE stack is hardwired to look for these constants
#define LED_RED              PIN_LED1
#define LED_BLUE             PIN_LED2

#define LED_STATE_ON         0         // State when LED is litted

/*
 * Analog pins
 */
#define PIN_A0               (2)
#define PIN_A1               (3)
#define PIN_A2               (4)
#define PIN_A3               (5)
#define PIN_A4               (28)
#define PIN_A5               (29)
#define PIN_A6               (30)
#define PIN_A7               (31)

static const uint8_t A0  = PIN_A0 ;
static const uint8_t A1  = PIN_A1 ;
static const uint8_t A2  = PIN_A2 ;
static const uint8_t A3  = PIN_A3 ;
static const uint8_t A4  = PIN_A4 ;
static const uint8_t A5  = PIN_A5 ;
static const uint8_t A6  = PIN_A6 ;
static const uint8_t A7  = PIN_A7 ;
#define ADC_RESOLUTION    14

// Other pins
#define PIN_AREF           (24)
#define PIN_VBAT           PIN_A7
#define PIN_NFC1           (9)
#define PIN_NFC2           (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL_RX       (28)
#define PIN_SERIAL_TX       (29)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

/*
 * This is the interal SPI bus for the RAK813 module - goes to the lora chip
 */

#define PIN_SPI_MISO         (12)
#define PIN_SPI_MOSI         (13)
#define PIN_SPI_SCK          (11)

static const uint8_t SS   = 14 ;
static const uint8_t MOSI = PIN_SPI_MOSI ;
static const uint8_t MISO = PIN_SPI_MISO ;
static const uint8_t SCK  = PIN_SPI_SCK ;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (15u)
#define PIN_WIRE_SCL         (16u)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
