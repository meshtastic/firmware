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

#ifndef _VARIANT_MINEWSEMI_MS24SF1_
#define _VARIANT_MINEWSEMI_MS24SF1_

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

#define PIN_LED1 (-1)

#define LED_PIN PIN_LED1
#define LED_BUILTIN -1

#define LED_BLUE -1
#define LED_STATE_ON 1 // State when LED is lit

#define BUTTON_PIN (-1)
#define BUTTON_NEED_PULLUP

#define HAS_WIRE 1

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 29) // P0.15
#define PIN_WIRE_SCL (0 + 30) // P0.17

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (-1) // P0.14
#define PIN_SERIAL1_TX (-1) // P0.13

#define PIN_SERIAL2_RX (-1) // P0.17
#define PIN_SERIAL2_TX (-1) // P0.16

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1 // 2

#define PIN_SPI_MISO (0 + 17) // MISO      P0.17
#define PIN_SPI_MOSI (0 + 20) // MOSI      P0.20
#define PIN_SPI_SCK (0 + 21)  // SCK       P0.21

// #define PIN_SPI1_MISO (-1) //
// #define PIN_SPI1_MOSI (10) // EPD_MOSI  P0.10
// #define PIN_SPI1_SCK (9)   // EPD_SCLK  P0.09

static const uint8_t SS = (0 + 22); // LORA_CS   P0.22
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// MINEWSEMI nRF52840+SX1262 MS24SF1 (NRF82540 with integrated SX1262)
#define USE_SX1262
#define SX126X_CS (0 + 22)    // LORA_CS     P0.22
#define SX126X_DIO1 (0 + 16)  // DIO1        P0.16
#define SX126X_BUSY (0 + 19)  // LORA_BUSY   P0.19
#define SX126X_RESET (0 + 12) // LORA_RESET  P0.12
#define SX126X_TXEN (32 + 4)  // TXEN        P1.04
#define SX126X_RXEN (32 + 2)  // RXEN        P1.02

// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH

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
// #define PIN_BUZZER (0 + 25)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_MINEWSEMI_MS24SF1_
