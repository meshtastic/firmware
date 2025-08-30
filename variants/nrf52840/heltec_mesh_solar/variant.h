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

#ifndef _VARIANT_HELTEC_NRF_
#define _VARIANT_HELTEC_NRF_
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


#define PIN_LED1 (0 + 12) // green (confirmed on 1.0 board)
#define LED_BLUE PIN_LED1 // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_BUILTIN LED_GREEN
#define LED_STATE_ON 0  // State when LED is lit

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA (32+15)                // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10)
// #define PIN_BUTTON2 (0 + 18)      // 0.18 is labeled on the board as RESET but we configure it in the bootloader as a regular
// GPIO

/*
No longer populated on PCB
*/
#define PIN_SERIAL2_RX (0 + 9)
#define PIN_SERIAL2_TX (0 + 10)
//  #define PIN_SERIAL2_EN (0 + 17)

/*
 * I2C
 */

#define WIRE_INTERFACES_COUNT 2

// I2C bus 0
// Routed to footprint for PCF8563TS RTC
// Not populated on T114 V1, maybe in future?
#define PIN_WIRE_SDA (0 + 6) // P0.26
#define PIN_WIRE_SCL (0 + 26) // P0.26

// I2C bus 1
// Available on header pins, for general use
#define PIN_WIRE1_SDA (0 + 30) // P0.30
#define PIN_WIRE1_SCL (0 + 5) // P0.13

/*
 * Lora radio
 */

#define USE_SX1262
// #define USE_SX1268
#define SX126X_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define LORA_CS  (0 + 24)
#define SX126X_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21)
// This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the
//    main
// CPU?
#define SX126X_BUSY  (0 + 17)
#define SX126X_RESET (0 + 25)
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/*
 * GPS pins
 */

#define GPS_L76K

// #define PIN_GPS_RESET (32 + 6) // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
// #define GPS_RESET_MODE LOW
// #define PIN_GPS_EN (21)
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define VEXT_ON_VALUE HIGH
// #define GPS_EN_ACTIVE HIGH
#define PIN_GPS_STANDBY (32 + 2) // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_GPS_PPS (32 + 4)
// Seems to be missing on this new board
// #define PIN_GPS_PPS (32 + 4)  // Pulse per second input from the GPS
#define GPS_TX_PIN (32 + 5) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (32 + 7) // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK  (0 + 19)

// #define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

#define BQ4050_SDA_PIN                      (32+1) // I2C data line pin
#define BQ4050_SCL_PIN                      (32+0) // I2C clock line pin
#define BQ4050_EMERGENCY_SHUTDOWN_PIN       (32+3) // Emergency shutdown pin

#define HAS_RTC 0
#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
