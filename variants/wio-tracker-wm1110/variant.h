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

#ifndef _VARIANT_WIO_TRACKER_WM1110_
#define _VARIANT_WIO_TRACKER_WM1110_

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

#define WIRE_INTERFACES_COUNT 1

#define PIN_3V3_EN (32 + 1) // P1.01, Power to Sensors

#define PIN_WIRE_SDA (0 + 5) // P0.05
#define PIN_WIRE_SCL (0 + 4) // P0.04

#define PIN_LED1 (0 + 6)      // P0.06
#define PIN_LED2 (PINS_COUNT) // P0.14

#define LED_BUILTIN PIN_LED1

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 0

#define BUTTON_PIN (32 + 2) // P1.02

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (0 + 24) // P0.24
#define PIN_SERIAL1_TX (0 + 25) // P0.25

#define PIN_SERIAL2_RX (0 + 6) // P0.06
#define PIN_SERIAL2_TX (0 + 8) // P0.08

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (32 + 15) // P1.15 47
#define PIN_SPI_MOSI (32 + 14) // P1.14 46
#define PIN_SPI_SCK (32 + 13)  // P1.13 45
#define PIN_SPI_NSS (32 + 12)  // P1.12 44

#define LORA_RESET (0 + 18) // P0.18 18 // RST
#define LORA_DIO1 (0 + 2)   // P0.02 2 // IRQ
#define LORA_DIO2 (32 + 11) // P1.11 43 // BUSY
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_CS PIN_SPI_NSS

// supported modules list
#define USE_LR1110

#define LR1110_IRQ_PIN LORA_DIO1
#define LR1110_NRESER_PIN LORA_RESET
#define LR1110_BUSY_PIN LORA_DIO2
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO

#define LR11X0_DIO3_TCXO_VOLTAGE 1.6
#define LR11X0_DIO_AS_RF_SWITCH

#define LR1110_GNSS_ANT_PIN (32 + 5) // P1.05 37

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_WIO_TRACKER_WM1110_
