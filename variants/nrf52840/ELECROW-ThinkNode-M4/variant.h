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

#ifndef _VARIANT_ELECROW_THINKNODE_M4_
#define _VARIANT_ELECROW_THINKNODE_M4_

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
#define PIN_LED2 (32 + 9)
#define LED_PAIRING (13)

#define Battery_LED_1 (15)
#define Battery_LED_2 (17)
#define Battery_LED_3 (32 + 2)
#define Battery_LED_4 (32 + 4)

#define LED_STATE_ON 1

// Button
#define PIN_BUTTON1 (4)

// Battery ADC
#define PIN_A0 (2)
#define BATTERY_PIN PIN_A0
#define BATTERY_SENSE_SAMPLES 30
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define ADC_MULTIPLIER (2.00F)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0

#define HAS_SERIAL_BATTERY_LEVEL 1
#define SERIAL_BATTERY_RX 30
#define SERIAL_BATTERY_TX 5

static const uint8_t A0 = PIN_A0;

#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// I2C
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (23)
#define PIN_WIRE_SCL (25)

// actually the LORA Radio
#define PIN_POWER_EN (11)

// charger status
#define EXT_CHRG_DETECT (32 + 6)
#define EXT_CHRG_DETECT_VALUE HIGH

// SPI
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (8)
#define PIN_SPI_MOSI (7)
#define PIN_SPI_SCK (6)

#define LORA_RESET (32 + 8)
#define LORA_DIO1 (12)
#define LORA_DIO2 (26)
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_CS (27)

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

// Peripherals on I2C bus. Active Low
#define VEXT_ENABLE (32)
#define VEXT_ON_VALUE LOW

// GPS L76K
#define HAS_GPS 1
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_EN (32 + 11)
#define GPS_EN_ACTIVE LOW
#define PIN_GPS_RESET (3)
#define GPS_RESET_MODE HIGH
#define PIN_GPS_STANDBY (28)
#define GPS_STANDBY_ACTIVE HIGH
#define GPS_TX_PIN (32 + 12)
#define GPS_RX_PIN (32 + 14)
#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

#ifdef __cplusplus
}
#endif

#endif
