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

#ifndef _VARIANT_LORA_RELAY_V1_
#define _VARIANT_LORA_RELAY_V1_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*
kevinh todo

ok leds
ok buttons
ok gps power
ok gps signal
ok? lcd
ok buzzer
serial flash
ok lora (inc boost en)

mention dat1 and dat2 on sd card
use hardware spi controller for lcd - not bitbang

*/

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (43)
#define NUM_DIGITAL_PINS (43)
#define NUM_ANALOG_INPUTS (6) // A6 is used for battery, A7 is analog reference
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (3)
#define PIN_LED2 (4)
// #define PIN_NEOPIXEL (8)
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 8                      // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define PIN_BUZZER (40)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1 (7)
#define PIN_BUTTON2 (35)
#define PIN_BUTTON3 (37)

/*
 * Analog pins
 */
#define PIN_A0 (16)
#define PIN_A1 (17)
#define PIN_A2 (18)
#define PIN_A3 (19)
#define PIN_A4 (20)
#define PIN_A5 (21)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF PIN_A5
#define PIN_VBAT PIN_A4
#define BATTERY_PIN PIN_VBAT
#define PIN_NFC1 (33)
#define PIN_NFC2 (2)
#define PIN_PIEZO (37)
static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (1)
#define PIN_SERIAL1_TX (0)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (24)
#define PIN_SPI_MOSI (25)
#define PIN_SPI_SCK (26)

static const uint8_t SS = (5);
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (22)
#define PIN_WIRE_SCL (23)

// I2C device addresses
#define I2C_ADDR_BQ27441 0x55 // Battery gauge

// SX1262 declaration
#define USE_SX1262

// CUSTOM GPIOs the SX1262
#define SX126X_CS (32)

// If you would prefer to get console debug output over the JTAG ICE connection rather than the CDC-ACM USB serial device, just
// define this. #define USE_SEGGER

#define SX126X_DIO1 (29)
#define SX1262_DIO2 (30)
#define SX126X_BUSY (33) // Supposed to be P0.18 but because of reworks, now on P0.31 (18)
#define SX126X_RESET (34)
// #define SX126X_ANT_SW (32 + 10)
#define SX126X_RXEN (14)
#define SX126X_TXEN (31)
#define SX126X_POWER_EN                                                                                                          \
    (15) // FIXME, see warning hre  https://github.com/BigCorvus/SX1262-LoRa-BLE-Relay/blob/master/LORA_RELAY_NRF52840.ino
// Indicates this SX1262 is inside of an ebyte E22 module and special config should be done for that
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ST7565 SPI
#define ST7735_RESET (11) // Output
#define ST7735_CS (12)
#define ST7735_BACKLIGHT_EN (13)
#define ST7735_RS (9)
#define ST7735_SDA (39) // actually spi MOSI
#define ST7735_SCK (37) // actually spi clk

#define PIN_GPS_EN 36   // Just kill GPS power when we want it to sleep?  FIXME
#define GPS_EN_ACTIVE 0 // GPS Power output is active low

// #define LORA_DISABLE_SENDING // The board can brownout during lora TX if you don't have a battery connected.  Disable sending
// to allow USB power only based debugging

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
