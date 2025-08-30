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

#ifndef _VARIANT_TTGO_EINK_V1_0_
#define _VARIANT_TTGO_EINK_V1_0_

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
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (32 + 7) // Green LED
#define PIN_LED2 (32 + 5) // Blue LED
// Unused(by firmware) LEDs:
#define PIN_LED3 (32 + 14) // Red LED inside, under the display.

#define LED_RED PIN_LED3
#define LED_BLUE PIN_LED2
#define LED_GREEN PIN_LED1

#define BLE_LED LED_BLUE
#define BLE_LED_INVERTED 1
#define LED_BUILTIN LED_GREEN
#define LED_CONN LED_GREEN
#define LED_STATE_ON 0 // State when LED is lit

// Buttons
#define PIN_BUTTON1 (0 + 24)
#define PIN_BUTTON2 (0 + 18) // 0.18 is labeled on the board as RESET but we configure it in the bootloader as a regular GPIO

#define BUTTON_CLICK_MS 400

// Analog pins
#define PIN_A0 (0 + 2) // Battery ADC

#define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

#define ADC_CTRL (0 + 31)
#define ADC_CTRL_ENABLED HIGH

// NFC
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// Wire Interfaces
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4)
#define PIN_WIRE_SCL (32 + 2)

/*
 Internal, PCB PAD interrupt PIN. Currently not used. (Not built in my device)
*/
// #define PIN_IMU_INT (0 + 16) // Interrupt from the IMU, macro name correct?!

// External serial flash ZD25WQ32CEIGR
// QSPI Pins
#define PIN_QSPI_SCK (0 + 4)
#define PIN_QSPI_CS (0 + 12)
#define PIN_QSPI_IO0 (0 + 6)  // MOSI if using two bit interface
#define PIN_QSPI_IO1 (0 + 8)  // MISO if using two bit interface
#define PIN_QSPI_IO2 (32 + 9) // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (0 + 26) // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES ZD25WQ32CEIGR
#define EXTERNAL_FLASH_USE_QSPI

// Lora radio

#define USE_SX1262
// #define USE_SX1268 // currently only available with XS1262.
#define SX126X_CS (0 + 11)
#define SX126X_DIO1 (32 + 8)
#define SX126X_DIO2 (0 + 5)
#define SX126X_BUSY (0 + 14)
#define SX126X_RESET (0 + 7)
#define SX126X_RXEN (32 + 1)
#define SX126X_TXEN (0 + 27)
// #define TCXO_OPTIONAL
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// eink display pins
#define VEXT_ENABLE (32 + 12)
#define VEXT_ON_VALUE LOW
#define PIN_EINK_CS (0 + 22)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_DC (0 + 21)
#define PIN_EINK_RES (0 + 28)
#define PIN_EINK_SCLK (0 + 19)
#define PIN_EINK_MOSI (0 + 20)

// Controls power 3V3 for all peripherals (eink + GPS + LoRa + Sensor)
#define PIN_POWER_EN (0 + 30) // 3V3 POWER Enable

#define PIN_SPI1_MISO (-1) // The display does not use MISO.
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

// GPS pins
// #define GPS_DEBUG
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define HAS_GPS 1
// #define PIN_GPS_REINIT (32 + 5) // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K

#define PIN_GPS_STANDBY (32 + 10) // An output to wake GPS, low means allow sleep, high means force wake
// Seems to be missing on this new board
#define PIN_GPS_PPS (0 + 29) // Pulse per second input from the GPS
#define GPS_TX_PIN (32 + 15) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (32 + 13) // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN

// SPI Interfaces
#define SPI_INTERFACES_COUNT 2

// For LORA, SPI 0
#define PIN_SPI_MISO (0 + 17)
#define PIN_SPI_MOSI (0 + 15)
#define PIN_SPI_SCK (0 + 13)

// Battery
// The battery sense is hooked to pin A0 (2)
// it is defined in the analogue pin section of this file
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

// #define NO_EXT_GPIO 1
// PINs back side
// Batt & solar connector left up corner
/*
-------------------------------
| VDDH, VBAT, 0.23, SCL , 1.06 |
| GND , SDA , 0.09, 0.10, 0.25 |
-------------------------------
                        --------
                        | VDDH |
                        | GND  |
                        | 1.13 | - Wake Up/standby
                        | 1.15 | - PPS
                        | 0.29 | - TX
                        | 1.10 | - RX
                        | 1.11 | - EN
                        --------
-------------------------------
| 3V3 , GND , 0.16, 1.03, G_WU |  0.16 internal solder pad interrupt PIN,
| G_EN, G_RX, G_TX, GND , PPS  |
-------------------------------
*/

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif