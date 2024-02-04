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

#ifndef _VARIANT_CANARYONE
#define _VARIANT_CANARYONE

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

#define CANARYONE

#define GPIO_PORT0 0
#define GPIO_PORT1 32

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (GPIO_PORT1 + 1)  // blue P1.01
#define PIN_LED2 (GPIO_PORT0 + 14) // yellow P0.14
#define PIN_LED3 (GPIO_PORT1 + 3)  // green P1.03

#define LED_BLUE PIN_LED1

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED3

#define LED_STATE_ON 0 // State when LED is lit
#define LED_INVERTED 1

/*
 * Buttons
 */
#define PIN_BUTTON1 (GPIO_PORT0 + 15) // BTN0 on schematic
#define PIN_BUTTON2 (GPIO_PORT0 + 16) // BTN1 on schematic

/*
 * Analog pins
 */
#define PIN_A0 (4) // Battery ADC P0.04

#define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

/**
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (GPIO_PORT0 + 26)
// #define I2C_SDA  (GPIO_PORT0 + 26)
#define PIN_WIRE_SCL (GPIO_PORT0 + 27)
// #define I2C_SCL (GPIO_PORT0 + 27)

#define PIN_LCD_RESET (GPIO_PORT0 + 2)

/*
 * External serial flash WP25R1635FZUIL0
 */

// QSPI Pins
#define PIN_QSPI_SCK (GPIO_PORT1 + 14)
#define PIN_QSPI_CS (GPIO_PORT1 + 15)
#define PIN_QSPI_IO0 (GPIO_PORT1 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (GPIO_PORT1 + 13) // MISO if using two bit interface
#define PIN_QSPI_IO2 (GPIO_PORT0 + 7)  // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (GPIO_PORT0 + 5)  // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*
 * Lora radio
 */
#define RADIOLIB_DEBUG 1
#define USE_SX1262
#define SX126X_CS (GPIO_PORT0 + 24)
#define SX126X_DIO1 (GPIO_PORT1 + 11)
// #define SX126X_DIO3 (GPIO_PORT0 + 21)
// #define SX126X_DIO2 () // LORA_BUSY // LoRa RX/TX
#define SX126X_BUSY (GPIO_PORT0 + 17)
#define SX126X_RESET (GPIO_PORT0 + 25)
#define LORA_RF_PWR (GPIO_PORT0 + 28) // LORA_RF_SWITCH

/*
 * GPS pins
 */
#define HAS_GPS 1
#define GPS_UBLOX
#define GPS_BAUDRATE 38400

// #define PIN_GPS_WAKE (GPIO_PORT1 + 2) // An output to wake GPS, low means allow sleep, high means force wake
// Seems to be missing on this new board
#define PIN_GPS_PPS (GPIO_PORT1 + 4) // Pulse per second input from the GPS
#define GPS_TX_PIN (GPIO_PORT1 + 9)  // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (GPIO_PORT1 + 8)  // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN

#define GPS_RESET_PIN (GPIO_PORT1 + 5) // GPS reset pin

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

// For LORA, spi 0
#define PIN_SPI_MISO (GPIO_PORT0 + 23)
#define PIN_SPI_MOSI (GPIO_PORT0 + 22)
#define PIN_SPI_SCK (GPIO_PORT0 + 19)

// #define PIN_SPI1_MISO (GPIO_PORT1 + 6) // FIXME not really needed, but for now the SPI code requires something to be defined,
//  pick an used GPIO #define PIN_SPI1_MOSI (GPIO_PORT1 + 8) #define PIN_SPI1_SCK (GPIO_PORT1 + 9)

#define PIN_PWR_EN (GPIO_PORT0 + 12)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
#define USE_SEGGER 1

// #define LORA_DISABLE_SENDING 1
#define SX126X_DIO2_AS_RF_SWITCH 1

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
// Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_MV_PER_LSB (0.73242188F)
// Voltage divider value => 100K + 100K voltage divider on VBAT = (100K / (100K + 100K))
#define VBAT_DIVIDER (0.5F)
// Compensation factor for the VBAT divider
#define VBAT_DIVIDER_COMP (2.0)
// Fixed calculation of milliVolt from compensation value
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif