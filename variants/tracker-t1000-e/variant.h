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

#ifndef _VARIANT_TRACKER_T1000_E_
#define _VARIANT_TRACKER_T1000_E_

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

#define PIN_3V3_EN (32 + 6)     // P1.6, Power to Sensors
#define PIN_3V3_ACC_EN (32 + 7) // P1.7, Power to Acc

#define PIN_LED1 (0 + 24) // P0.24
#define LED_PIN PIN_LED1
#define LED_BUILTIN -1
#define LED_BLUE -1    // Actually green
#define LED_STATE_ON 1 // State when LED is lit

#define BUTTON_PIN (0 + 6) // P0.06
#define BUTTON_ACTIVE_LOW false
#define BUTTON_ACTIVE_PULLUP false
#define BUTTON_SENSE_TYPE 0x6

#define HAS_WIRE 1

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 26)      // P0.26
#define PIN_WIRE_SCL (0 + 27)      // P0.27
#define I2C_NO_RESCAN              // I2C is a bit finicky, don't scan too much
#define HAS_QMA6100P               // very rare beast, only on this board.
#define QMA_6100P_INT_PIN (32 + 2) // P1.02

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (0 + 14) // P0.14
#define PIN_SERIAL1_TX (0 + 13) // P0.13

#define PIN_SERIAL2_RX (0 + 17) // P0.17
#define PIN_SERIAL2_TX (0 + 16) // P0.16

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (32 + 8) // P1.08
#define PIN_SPI_MOSI (32 + 9) // P1.09
#define PIN_SPI_SCK (0 + 11)  // P0.11
#define PIN_SPI_NSS (0 + 12)  // P0.12

#define LORA_RESET (32 + 10) // P1.10 // RST
#define LORA_DIO1 (32 + 1)   // P1.01 // IRQ
#define LORA_DIO2 (0 + 7)    // P0.07 // BUSY
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

#define HAS_GPS 1
#define GNSS_AIROHA
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

#define GPS_BAUDRATE 115200

#define PIN_GPS_EN (32 + 11) // P1.11
#define GPS_EN_ACTIVE HIGH

#define PIN_GPS_RESET (32 + 15) // P1.15
#define GPS_RESET_MODE HIGH

#define GPS_VRTC_EN (0 + 8)      // P0.8, always high
#define GPS_SLEEP_INT (32 + 12)  // P1.12, always high
#define GPS_RTC_INT (0 + 15)     // P0.15, normal is LOW, wake by HIGH
#define GPS_RESETB_OUT (32 + 14) // P1.14, always input pull_up

#define GPS_FIX_HOLD_TIME 15000 // ms
#define BATTERY_PIN 2           // P0.02/AIN0, BAT_ADC
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER (2.0F)
// P0.04/AIN2 is VCC_ADC, P0.05/AIN3 is CHARGER_DET, P1.03 is CHARGE_STA, P1.04 is CHARGE_DONE

#define EXT_CHRG_DETECT (32 + 3) // P1.03
#define EXT_CHRG_DETECT_VALUE LOW
// #define EXT_IS_CHRGD (32 + 4)  // P1.04
// #define EXT_IS_CHRGD_VALUE LOW
#define EXT_PWR_DETECT (0 + 5) // P0.05

#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12

#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0

// Buzzer
#define BUZZER_EN_PIN (32 + 5) // P1.05, always high
#define PIN_BUZZER (0 + 25)    // P0.25, pwm output

#define T1000X_SENSOR_EN
#define T1000X_VCC_PIN (0 + 4)  // P0.4
#define T1000X_NTC_PIN (0 + 31) // P0.31/AIN7
#define T1000X_LUX_PIN (0 + 29) // P0.29/AIN5

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_TRACKER_T1000_E_