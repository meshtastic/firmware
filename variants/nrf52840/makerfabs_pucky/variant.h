
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

#ifndef _VARIANT_RAK4630_
#define _VARIANT_RAK4630_

// #define RAK4630

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

// #define LED_PIN 16 // This is a LED_WS2812 not a standard LED
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 25                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define RGB_LED_POWER

#define LED_STATE_ON 1

/*
 * Buttons
 */

#define BUTTON_PIN 5
#define PIN_BUZZER 22
/*
 * Analog pins
 */
#define PIN_A0 (2)
#define PIN_A1 (3)
#define PIN_A2 (4)
#define PIN_A3 (5)
#define PIN_A4 (28)
#define PIN_A5 (29)
#define PIN_A6 (30)
#define PIN_A7 (31)

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

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
// GPS on Base Board
#define PIN_SERIAL1_RX (8)
#define PIN_SERIAL1_TX (6)

// DEBUG on Base Board
#define PIN_SERIAL2_RX (38)
#define PIN_SERIAL2_TX (37)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (47)
#define PIN_SPI_MOSI (46)
#define PIN_SPI_SCK (45)

static const uint8_t SS = 44;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#define USE_LR1121
#define LR11X0_POWER_EN (13)
#define LR1121_IRQ_PIN 14
#define LR1121_NRESET_PIN (42)
#define LR1121_BUSY_PIN (43)
#define LR1121_SPI_NSS_PIN SS
#define LR1121_SPI_SCK_PIN PIN_SPI_SCK
#define LR1121_SPI_MOSI_PIN PIN_SPI_MOSI
#define LR1121_SPI_MISO_PIN PIN_SPI_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
// #define LR11X0_DIO_AS_RF_SWITCH

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (26)
#define PIN_WIRE_SCL (27)

#define HAS_QMA6100P

/*
 * Power Switches
 */
#define V_RFSW (13)
#define POWER_ON_OFF (30)
#define PIN_3V3_EN (16)

#define HAS_GPS 1
#define PIN_GPS_PPS (11) // Pulse per second input from the GPS
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// Battery
#define EXT_CHRG_DETECT (32 + 7) // P1.07
#define HAS_CW2015 1

// RAK4630 AIN0 = nrf52840 AIN3 = Pin 5
// #define BATTERY_LPCOMP_INPUT NRF_LPCOMP_INPUT_3

// We have AIN3 with a VBAT divider so AIN3 = VBAT * (1.5/2.5)
// We have the device going deep sleep under 3.1V, which is AIN3 = 1.86V
// So we can wake up when VBAT>=VDD is restored to 3.3V, where AIN3 = 1.98V
// 1.98/3.3 = 6/10, but that's close to the VBAT divider, so we
// pick 6/8VDD, which means VBAT=4.1V.
// Reference:
// VDD=3.3V AIN3=5/8*VDD=2.06V VBAT=1.66*AIN3=3.41V
// VDD=3.3V AIN3=11/16*VDD=2.26V VBAT=1.66*AIN3=3.76V
// VDD=3.3V AIN3=6/8*VDD=2.47V VBAT=1.66*AIN3=4.1V
// #define BATTERY_LPCOMP_THRESHOLD NRF_LPCOMP_REF_SUPPLY_11_16

#define HOCKEY_PUCK 1
#define TX_GAIN_LORA 0

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif