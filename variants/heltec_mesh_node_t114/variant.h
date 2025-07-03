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

#define HELTEC_MESH_NODE_T114

#define USE_ST7789

#define ST7789_NSS 11
#define ST7789_RS 12  // DC
#define ST7789_SDA 41 // MOSI
#define ST7789_SCK 40
#define ST7789_RESET 2
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define VTFT_CTRL 3
#define VTFT_LEDA 15
// #define ST7789_BL (32+6)
#define TFT_BACKLIGHT_ON LOW
#define ST7789_SPI_HOST SPI1_HOST
// #define TFT_BL (32+6)
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0

// T114 gets a muted yellow on black display
#define TFT_MESH_OVERRIDE COLOR565(255, 255, 128)

// #define TFT_OFFSET_ROTATION 0
// #define SCREEN_ROTATE
// #define SCREEN_TRANSITION_FRAMERATE 5

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (32 + 3) // green (confirmed on 1.0 board)
#define LED_BLUE PIN_LED1 // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_BUILTIN LED_GREEN
#define LED_STATE_ON 0 // State when LED is lit

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 2                     // How many neopixels are connected
#define NEOPIXEL_DATA 14                     // gpio pin used to send data to the neopixels
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
#define PIN_WIRE_SDA (0 + 26) // P0.26
#define PIN_WIRE_SCL (0 + 27) // P0.27

// I2C bus 1
// Available on header pins, for general use
#define PIN_WIRE1_SDA (0 + 16) // P0.16
#define PIN_WIRE1_SCL (0 + 13) // P0.13

// QSPI Pins
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_IO0 (32 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (32 + 13) // MISO if using two bit interface
#define PIN_QSPI_IO2 (0 + 7)   // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (0 + 5)   // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*
 * Lora radio
 */

#define USE_SX1262
// #define USE_SX1268
#define SX126X_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define LORA_CS (0 + 24)
#define SX126X_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21)
// This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the
//    main
// CPU?
#define SX126X_BUSY (0 + 17)
#define SX126X_RESET (0 + 25)
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_SPI1_MISO                                                                                                            \
    ST7789_MISO // FIXME not really needed, but for now the SPI code requires something to be defined, pick an used GPIO
#define PIN_SPI1_MOSI ST7789_SDA
#define PIN_SPI1_SCK ST7789_SCK

/*
 * GPS pins
 */

#define GPS_L76K

// #define PIN_GPS_RESET (32 + 6) // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
#define GPS_RESET_MODE LOW
// #define PIN_GPS_EN (21)
#define VEXT_ENABLE (0 + 21)
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

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 19)

// #define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution

#define ADC_CTRL 6
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 4
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.90F)

#define HAS_RTC 0
#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
