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

#ifndef _VARIANT_ELECROW_THINKNODE_M6_
#define _VARIANT_ELECROW_THINKNODE_M6_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define LED_BUILTIN -1
#define LED_BLUE -1
#define LED_CHARGE (12)
#define LED_PAIRING (7)

#define LED_STATE_ON 1

// USB power detection
#define EXT_PWR_DETECT (13)

// Button
#define PIN_BUTTON1 (17)

// Battery ADC
#define PIN_A0 (28)
#define BATTERY_PIN PIN_A0
#define ADC_CTRL (11)
#define ADC_CTRL_ENABLED 1

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14
#define BATTERY_SENSE_SAMPLES 30

#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// I2C
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (32 + 9)
#define PIN_WIRE_SCL (8)

// Peripheral power enable
#define PIN_POWER_EN (27)

// Solar charger status
#define EXT_CHRG_DETECT (15)
#define EXT_CHRG_DETECT_VALUE LOW

// QSPI Flash
#define PIN_QSPI_SCK (32 + 3)
#define PIN_QSPI_CS (23)
#define PIN_QSPI_IO0 (32 + 1)
#define PIN_QSPI_IO1 (32 + 2)
#define PIN_QSPI_IO2 (32 + 4)
#define PIN_QSPI_IO3 (32 + 5)

#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI
#define VDD_FLASH_EN (21)

// LoRa SX1262
#define USE_SX1262
#define SX126X_CS (32 + 12)
#define SX126X_DIO1 (32 + 6)
#define SX126X_BUSY (32 + 11)
#define SX126X_RESET (32 + 10)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3

// GPS L76K
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_EN (6)
#define PIN_GPS_REINIT (29)
#define PIN_GPS_STANDBY (30)
#define PIN_GPS_PPS (31)
#define GPS_TX_PIN (2)
#define GPS_RX_PIN (3)
#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

// Secondary UART
#define PIN_SERIAL2_RX (22)
#define PIN_SERIAL2_TX (24)

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

// SPI
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (32 + 15)
#define PIN_SPI_MOSI (32 + 14)
#define PIN_SPI_SCK (32 + 13)

// Battery
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (1.75F)

#define HAS_SOLAR

#ifdef __cplusplus
}
#endif

#endif
