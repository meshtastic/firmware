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

#ifndef _VARIANT_TRACKER_T2000_C_
#define _VARIANT_TRACKER_T2000_C_

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

#define SERIAL_PRINT_PORT 0

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// Use the native nrf52 usb power detection
#define NRF_APM

// Power control
#define PIN_PWR_CTR (0 + 12) // P0.12, main power control

// LEDs (active HIGH)
#define PIN_LED1 (32 + 2) // P1.02, Green
#define PIN_LED2 (32 + 3) // P1.03, Blue
#define PIN_LED3 (0 + 19) // P0.19, Red
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#define LED_RED PIN_LED3
#define LED_POWER PIN_LED1
#define LED_STATE_ON 1 // State when LED is lit

// Button
#define BUTTON_PIN (0 + 11) // P0.11, USER_BTN
#define BUTTON_ACTIVE_LOW false
#define BUTTON_ACTIVE_PULLUP false
#define BUTTON_SENSE_TYPE 0x5 // enable input pull-down

// Theft protection / tamper switch
#define TP_BUTTON_PIN (0 + 8) // P0.08

// Default detection sensor pin: tamper switch
#define DETECTION_SENSOR_DEFAULT_PIN TP_BUTTON_PIN

// I2C
#define HAS_WIRE 1
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 6) // P0.06
#define PIN_WIRE_SCL (0 + 5) // P0.05
#define I2C_NO_RESCAN

// Accelerometer enable (used in initVariant)
#define ACC_EN_PIN (0 + 15) // P0.15

/*
 * Serial interfaces
 */
// Serial1: GPS (GNSS)
#define PIN_SERIAL1_RX (0 + 26) // P0.26
#define PIN_SERIAL1_TX (0 + 27) // P0.27

/*
 * SPI Interfaces (LoRa SX1262)
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 3)  // P0.03
#define PIN_SPI_MOSI (0 + 28) // P0.28
#define PIN_SPI_SCK (0 + 30)  // P0.30
#define PIN_SPI_NSS (32 + 14) // P1.14

// LoRa SX1262
#define LORA_RESET (32 + 7) // P1.07, RST
#define LORA_DIO1 (0 + 7)   // P0.07, IRQ/DIO1
#define LORA_DIO2 (32 + 10) // P1.10, BUSY
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_CS PIN_SPI_NSS

// Supported modules list
#define USE_SX1262

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// GPS L76K
#define HAS_GPS 1
#define GPS_L76K
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

#define GPS_BAUDRATE 9600

#define PIN_GPS_EN (32 + 5) // P1.05, GNSS power control
#define GPS_EN_ACTIVE HIGH

#define PIN_GPS_RESET (32 + 6) // P1.06, GNSS reset
#define GPS_RESET_MODE HIGH

#define PIN_GPS_STANDBY (32 + 9) // P1.09, GNSS wakeup/standby

// Battery
#define BATTERY_PIN (0 + 31) // P0.31/AIN7, BAT_ADC
#define ADC_CTRL (0 + 2)     // P0.02, BAT_ADC enable
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER (2.0F)

#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12

#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0

#define OCV_ARRAY 4000, 3951, 3910, 3867, 3772, 3687, 3593, 3519, 3463, 3297, 3100

#define nRF_FLASH_POWER_CTR (32 + 13) // P1.13

#define HAS_SCREEN 0

// Enable Traffic Management Module
// NRF52840 has 256KB RAM - 1024 entries uses ~10KB
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 1
#endif
#ifndef TRAFFIC_MANAGEMENT_CACHE_SIZE
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 1024
#endif

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_TRACKER_T2000_C_
