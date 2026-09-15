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

/*
  RAK3401 (nRF52840 WisBlock Core) + RAK13700 (LR2021).
  Core / GPS / I2C / battery / eink pin map aligned with rak3401_1watt;
  radio macros are LR2021 instead of SX1262 (RAK13302).
*/

#ifndef _VARIANT_RAK3401_LR2021_
#define _VARIANT_RAK3401_LR2021_

#define RAK4630

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define LED_BLUE (36)
#define LED_GREEN PIN_LED1
#define LED_NOTIFICATION LED_BLUE
#define LED_STATE_ON 1

/*
 * Analog pins
 */
#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins (same as rak3401_1watt)
#define WB_I2C1_SDA (13) // SENSOR_SLOT IO_SLOT
#define WB_I2C1_SCL (14) // SENSOR_SLOT IO_SLOT

#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define WB_IO5 PIN_NFC1
#define WB_IO4 (4)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)

#define PIN_SPI1_MISO (29)
#define PIN_SPI1_MOSI (30)
#define PIN_SPI1_SCK (3)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins - same mapping as rak3401_1watt.
 * Note: CS/BUSY/SCLK/MOSI overlap IO-slot LoRa SPI pins; only safe when an
 * e-ink module is present and firmware uses CS gating (same as 1watt).
 */
#define PIN_EINK_CS (0 + 26)
#define PIN_EINK_BUSY (0 + 4)
#define PIN_EINK_DC (0 + 17)
#define PIN_EINK_RES (-1)
#define PIN_EINK_SCLK (0 + 3)
#define PIN_EINK_MOSI (0 + 30)

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (WB_I2C1_SDA)
#define PIN_WIRE_SCL (WB_I2C1_SCL)

// QSPI Pins / on-board flash
#define PIN_QSPI_SCK 3
#define PIN_QSPI_CS 26
#define PIN_QSPI_IO0 30
#define PIN_QSPI_IO1 29
#define PIN_QSPI_IO2 28
#define PIN_QSPI_IO3 2
#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

/*
 * RAK13700 (LR2021) on WisBlock IO slot
 * GPIO numbers match RAK13302 on the same slot (rak3401_1watt).
 */
#define HW_SPI1_DEVICE 1

#define LORA_SCK PIN_SPI1_SCK
#define LORA_MISO PIN_SPI1_MISO
#define LORA_MOSI PIN_SPI1_MOSI
#define LORA_CS 26
#define LORA_RESET 4
#define LORA_DIO1 10 // IRQ (module DIO8)
#define LORA_DIO2 9  // BUSY

#define USE_LR2021
#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN LORA_RESET
#define LR2021_BUSY_PIN LORA_DIO2
#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_SPI_SCK_PIN LORA_SCK
#define LR2021_SPI_MOSI_PIN LORA_MOSI
#define LR2021_SPI_MISO_PIN LORA_MISO
#define LR2021_IRQ_DIO_NUM 8

// Stable default on current samples; see docs/lr2021_tcxo_calibrate_707.md.
#define LR2021_DIO3_TCXO_VOLTAGE 1.6f
#define LR2021_DIO_AS_RF_SWITCH
#define LR2021_CUSTOM_PA_TABLE // board LF PA table - see pa_table.h

// UNCERTAIN: same as RAK13302 SX126X_POWER_EN (WB IO3)
#define LR2021_POWER_EN (21)
#define LR2021_MAX_POWER 22
#define LR2021_MAX_POWER_HF 12

#define NRF_APM

// enables 3.3V periphery like GPS or IO Module
#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

// RAK1910 GPS on Port A (UART1); power stays on 3V3_S (WB_IO2) - do not use as GPS reset
#define PIN_GPS_PPS (17)
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// RAK12002 RTC Module
#define RV3028_RTC (uint8_t)0b1010010

// Battery
#define BATTERY_PIN PIN_A0
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.73

#define RAK_4631 1

#ifdef __cplusplus
}
#endif

#endif
