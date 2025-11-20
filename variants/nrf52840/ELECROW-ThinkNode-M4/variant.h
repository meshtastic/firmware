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

#define USE_LFXO // Board uses 32 kHz crystal for LF domain

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

// LEDs
#define PIN_LED1 (32 + 9) // P1.09, status LED (blue)
#define PIN_LED2 -1
#define PIN_LED3 -1

#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 0 // active-low LED drive

// Buttons
#define PIN_BUTTON1 (0 + 4) // P0.04 KEY input

// USB / power detection
#define EXT_PWR_DETECT (32 + 3) // P1.03 USB present sense

// I2C bus
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (32 + 0) // P1.00 SDA1
#define PIN_WIRE_SCL (32 + 5) // P1.05 SCL1

// Analog pins
#define PIN_A0 (2) // P0.02
#define BATTERY_PIN PIN_A0
#define BATTERY_SENSE_SAMPLES 30
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define ADC_MULTIPLIER (2.00F)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0

// GPS interface (L76K)
#define HAS_GPS 1
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_REINIT (0 + 3)   // P0.03 GPS_RST
#define PIN_GPS_STANDBY (0 + 28) // P0.28 GPS_STANDBY
#define PIN_GPS_EN (32 + 11)     // P1.11 GPS_EN
#define GPS_EN_ACTIVE HIGH
#define GPS_TX_PIN (32 + 12) // P1.12 GPS_TX -> MCU RX
#define GPS_RX_PIN (32 + 14) // P1.14 GPS_RX -> MCU TX
#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN
#define GPS_THREAD_INTERVAL 50

// LoRa (LR1110) SPI bus
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (0 + 8) // P0.08
#define PIN_SPI_MOSI (0 + 7) // P0.07
#define PIN_SPI_SCK (0 + 6)  // P0.06
#define PIN_SPI_NSS (0 + 27) // P0.27

#define LORA_CS PIN_SPI_NSS
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_RESET (32 + 8) // P1.08 LR_NRESET
#define LORA_DIO1 (0 + 24)  // P0.24 LR_DIO1 / IRQ
#define LORA_DIO2 (0 + 26)  // P0.26 LR_BUSY (treated as DIO2)

#define USE_LR1110
#define LR1110_IRQ_PIN LORA_DIO1
#define LR1110_NRESET_PIN LORA_RESET
#define LR1110_BUSY_PIN LORA_DIO2
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.3f
#define LR11X0_DIO_AS_RF_SWITCH

// Peripheral power control
#define PIN_POWER_EN (0 + 11) // P0.11 PERIPH_EN

#ifdef __cplusplus
}
#endif

#endif // _VARIANT_ELECROW_THINKNODE_M4_
