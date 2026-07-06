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

#ifndef _VARIANT_HELTEC_MESH_NODE_T1_
#define _VARIANT_HELTEC_MESH_NODE_T1_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO // Board uses 32khz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define HELTEC_MESH_NODE_T1

// Display (ST7735, 80x160 TFT via SPI1)
#define HAS_SPI_TFT 1
#define ST7735_CS (0 + 12)
#define ST7735_RS (0 + 22) // DC
#define ST7735_SDA (0 + 24)
#define ST7735_SCK (32 + 0)
#define ST7735_RESET (0 + 20)
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define ST7735_BL (0 + 15)
#define VTFT_CTRL (0 + 13) // Active HIGH, powers the ST7735 display
#define SPI_FREQUENCY 80000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT 160
#define TFT_WIDTH 80
#define TFT_OFFSET_X 24
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define DISPLAY_FORCE_SMALL_FONTS
#define FORCE_LOW_RES 1 // 80px-wide panel causes artifacts with full-res UI elements

// Pins

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs

#define PIN_LED1 (0 + 16)
#define LED_BLUE PIN_LED1 // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_STATE_ON 0 // State when LED is lit

// Buttons

#define PIN_BUTTON1 (32 + 10)
#define PIN_BUTTON2 (0 + 14)

// Serial (unused, not populated on PCB)

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

// I2C (ICM42607P IMU and MMC5983MA compass)

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (32 + 3)
#define PIN_WIRE_SCL (0 + 10)

#define PIN_SENSOR_EN (32 + 6) // Active LOW - controls IMU and compass VDD
#define PIN_SENSOR_EN_ACTIVE LOW

// ICM42607P interrupt pins - populated on PCB, not yet used in firmware
// #define ICM_42607P_INT_PIN  (32 + 1) // INT1 - P1.01
// #define ICM_42607P_INT2_PIN (32 + 7) // INT2 - P1.07

// LoRa (SX1262)

#define USE_SX1262
#define SX126X_CS (32 + 11) // FIXME - we really should define LORA_CS instead
#define LORA_CS SX126X_CS
#define SX126X_DIO1 (0 + 31)
#define SX126X_BUSY (0 + 29)
#define SX126X_RESET (0 + 2)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// SPI

#define SPI_INTERFACES_COUNT 2

// SPI0 - LoRa
#define PIN_SPI_MISO (0 + 3)
#define PIN_SPI_MOSI (32 + 14)
#define PIN_SPI_SCK (32 + 13)

// SPI1 - Display (ST7735, write-only)
#define PIN_SPI1_MISO ST7735_MISO
#define PIN_SPI1_MOSI ST7735_SDA
#define PIN_SPI1_SCK ST7735_SCK

// GPS (UC6580)

#define GPS_UC6580
#define GPS_BAUDRATE 115200
#define PIN_GPS_RESET (0 + 26)
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (0 + 4)
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000 // Allow I2C bus to stabilise after sensor power-on
#define PIN_GPS_PPS (32 + 9)      // Pulse per second input from the GPS
#define GPS_TX_PIN (0 + 7)
#define GPS_RX_PIN (0 + 8)
#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

// Buzzer

#define PIN_BUZZER (0 + 9)
#define PIN_BUZZER_VOLTAGE_MULTIPLIER_1 (32 + 2)
#define PIN_BUZZER_VOLTAGE_MULTIPLIER_2 (32 + 5)

// Battery / ADC

#define ADC_CTRL 11
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 5 // nRF52840 AIN3
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.916F)

// #define BATTERY_LPCOMP_INPUT NRF_LPCOMP_INPUT_3 // UNSAFE: causes 2.9 mA deep-sleep leakage (issue #8801)

// Power / USB

#define NRF_APM   // USB VBUS detection via nrfx_power_usbstatus_get() - no dedicated charging IC on this board
#define HAS_RTC 0 // No external RTC fitted

#ifdef __cplusplus
}
#endif

#endif
