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

#ifndef _VARIANT_ELECROW_EINK_V1_0_
#define _VARIANT_ELECROW_EINK_V1_0_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "WVariant.h"

#define VARIANT_MCK (64000000ul)
#define USE_LFXO // Board uses 32khz crystal for LF

#define ELECROW_ThinkNode_M3 1
// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// Power Pin
#define NRF_APM
#define GPS_STD_POWER 6
#define GPS_POWER 14
#define PIN_POWER_USB 31
// #define PIN_POWER_USB 29
#define PIN_POWER_DONE 24
#define PIN_POWER_CHRG 32
#define KEY_POWER 16
#define ACC_POWER 2
#define DHT_POWER 3
#define Battery_POWER 17
#define RGB_POWER 29
#define EEPROM_POWER 7
// LED
#define red_LED_PIN 33
#define LED_POWER red_LED_PIN
#define green_LED_PIN 35
#define LED_BLUE 37

#define LED_BUILTIN green_LED_PIN
#define LED_STATE_ON LOW
#define LED_STATE_OFF HIGH
#define BLE_LED LED_BLUE
#define BLE_LED_INVERTED
// BUZZER
#define PIN_BUZZER 23
#define PIN_EN1 36
#define PIN_EN2 34
/*Wire Interfaces*/
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 26
#define PIN_WIRE_SCL 27
/*GPS pins*/
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define PIN_GPS_RESET 25
#define PIN_GPS_STANDBY 21
#define GPS_TX_PIN 20
#define GPS_RX_PIN 22
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN
// Button
#define BUTTON_PIN 12
#define BUTTON_PIN_ALT (0 + 12)
// Battery
#define BATTERY_PIN 5
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 2.4
#define VBAT_AR_INTERNAL AR_INTERNAL_2_4
#define ADC_MULTIPLIER (1.75)
/*SPI Interfaces*/
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (32 + 15) // P1.15 47
#define PIN_SPI_MOSI (32 + 14) // P1.14 46
#define PIN_SPI_SCK (32 + 13)  // P1.13 45
#define PIN_SPI_NSS (32 + 12)  // P1.12 44
/*LORA Interfaces*/
#define USE_LR1110
#define LR1110_IRQ_PIN 40
#define LR1110_NRESET_PIN 42
#define LR1110_BUSY_PIN 43
#define LR1110_SPI_NSS_PIN 44
#define LR1110_SPI_SCK_PIN 45
#define LR1110_SPI_MOSI_PIN 46
#define LR1110_SPI_MISO_PIN 47
#define LR11X0_DIO3_TCXO_VOLTAGE 3.3
#define LR11X0_DIO_AS_RF_SWITCH

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

#ifdef __cplusplus
}
#endif

#endif
