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

#ifndef _VARIANT_SENSECAP_CARD_TRACKER_T1000_E_
#define _VARIANT_SENSECAP_CARD_TRACKER_T1000_E_

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

#define PIN_3V3_EN (32 + 1) // P1.1, Power to Sensors

#define PIN_WIRE_SDA (0 + 5) // P0.5
#define PIN_WIRE_SCL (0 + 4) // P0.4

#define PIN_LED1 (0 + 6) // P0.6
#define PIN_LED2 (0 + 6) // P0.6

#define LED_BUILTIN PIN_LED1

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2 // Actually red

#define LED_STATE_ON 1 // State when LED is lit

#define BUTTON_PIN (32 + 2) // P1.2

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (0 + 30) // P0.30
#define PIN_SERIAL1_TX (0 + 31) // P0.31

#define PIN_SERIAL2_RX (0 + 24) // P0.24
#define PIN_SERIAL2_TX (0 + 25) // P0.25


#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (32 + 15) // P1.15 47
#define PIN_SPI_MOSI (32 + 14) // P1.14 46
#define PIN_SPI_SCK (32 + 13)  // P1.13 45
#define PIN_SPI_NSS (32 + 12)  // P1.12 44

#define LORA_RESET (32 + 10) // P1.10 42 // RST
#define LORA_DIO1 (0 + 2)    // P0.02 2 // IRQ
#define LORA_DIO2 (32 + 11)  // P1.11 43 // BUSY
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
#define LR11X0_DIO_RF_SWITCH_CONFIG 0x03, 0x0, 0x01, 0x03, 0x02, 0x0, 0x0, 0x0

#define LR1110_GNSS_ANT_PIN (32 + 5) // P1.05 37

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif 
