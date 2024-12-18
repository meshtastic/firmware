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

#ifndef _VARIANT_ME25LS01_4Y10TD_
#define _VARIANT_ME25LS01_4Y10TD_

#define ME25LS01

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

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

// Use the native nrf52 usb power detection
#define NRF_APM

#define PIN_3V3_EN (32 + 5) //-1
#define PIN_3V3_ACC_EN -1

#define PIN_LED1 (32 + 7) // P1.07 Blue D2

#define LED_PIN PIN_LED1
#define LED_BUILTIN -1

#define LED_BLUE -1
#define LED_STATE_ON 1 // State when LED is lit

#define BUTTON_PIN (0 + 27) // P0.27 K3
#define BUTTON_NEED_PULLUP

#define HAS_WIRE 1

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 15) // P0.15
#define PIN_WIRE_SCL (0 + 17) // P0.17

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (0 + 14) // P0.14
#define PIN_SERIAL1_TX (0 + 13) // P0.13

#define PIN_SERIAL2_RX (0 + 17) // P0.17
#define PIN_SERIAL2_TX (0 + 16) // P0.16

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 29) // P0.20 // MISO
#define PIN_SPI_MOSI (0 + 2)  // P0.02 // MOSI
#define PIN_SPI_SCK (32 + 15) // P1.15 // SCK
#define PIN_SPI_NSS (32 + 13) // P1.13 // NSS

#define LORA_RESET (32 + 11) // P1.11 // RST
#define LORA_DIO1 (32 + 12)  // P1.12 // IRQ
#define LORA_DIO2 (32 + 10)  // P1.10 // BUSY
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_CS PIN_SPI_NSS

// supported modules list
#define USE_LR1110

#define LR1110_IRQ_PIN LORA_DIO1
#define LR1110_NRESET_PIN LORA_RESET
#define LR1110_BUSY_PIN LORA_DIO2
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO

#define LR11X0_DIO3_TCXO_VOLTAGE 1.6
#define LR11X0_DIO_AS_RF_SWITCH

#define HAS_GPS 0

#define PIN_GPS_EN -1
#define GPS_EN_ACTIVE HIGH
#define PIN_GPS_RESET -1
#define GPS_VRTC_EN -1
#define GPS_SLEEP_INT -1
#define GPS_RTC_INT -1
#define GPS_RESETB_OUT -1

#define BATTERY_PIN -1
#define ADC_MULTIPLIER (2.0F)

#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12

#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0

// Buzzer
#define PIN_BUZZER (0 + 25)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_ME25LS01_4Y10TD_
