/*

TTGO T-BEAM Tracker for The Things Network

Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>

This code requires LMIC library by Matthijs Kooijman
https://github.com/matthijskooijman/arduino-lmic

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#pragma once

#include <Arduino.h>
// -----------------------------------------------------------------------------
// Version
// -----------------------------------------------------------------------------

// If app version is not specified we assume we are not being invoked by the build script
#ifndef APP_VERSION
#error APP_VERSION, HW_VERSION, and HW_VERSION_countryname must be set by the build environment
//#define APP_VERSION 0.0.0   // this def normally comes from build-all.sh
//#define HW_VERSION 1.0 - US // normally comes from build-all.sh and contains the region code
#endif

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// If we are using the JTAG port for debugging, some pins must be left free for that (and things like GPS have to be disabled)
// we don't support jtag on the ttgo - access to gpio 12 is a PITA
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V2
//#define USE_JTAG
#endif

#define REQUIRE_RADIO true // If true, we will fail to start if the radio is not found

/// Convert a preprocessor name into a quoted string
#define xstr(s) str(s)
#define str(s) #s

/// Convert a preprocessor name into a quoted string and if that string is empty use "unset"
#define optstr(s) (xstr(s)[0] ? xstr(s) : "unset")

#ifdef PORTDUINO

#define NO_ESP32 // Don't use ESP32 libs (mainly bluetooth)

#elif defined(NRF52_SERIES) // All of the NRF52 targets are configured using variant.h, so this section shouldn't need to be
// board specific

//
// Standard definitions for NRF52 targets
//

#define NO_ESP32 // Don't use ESP32 libs (mainly bluetooth)

// We bind to the GPS using variant.h instead for this platform (Serial1)

#define LED_PIN PIN_LED1 // LED1 on nrf52840-DK

// If the variant filed defines as standard button
#ifdef PIN_BUTTON1
#define BUTTON_PIN PIN_BUTTON1
#endif

// FIXME, use variant.h defs for all of this!!! (even on the ESP32 targets)
#elif defined(CubeCell_BoardPlus)

//
// Standard definitions for CubeCell targets
//

#define NO_ESP32 // Don't use ESP32 libs (mainly bluetooth)

#define LED_PIN -1 // FIXME totally bogus
#define BUTTON_PIN -1

#else

//
// Standard definitions for ESP32 targets
//

#define GPS_SERIAL_NUM 1
#define GPS_RX_PIN 34
#ifdef USE_JTAG
#define GPS_TX_PIN -1
#else
#define GPS_TX_PIN 12
#endif

// -----------------------------------------------------------------------------
// LoRa SPI
// -----------------------------------------------------------------------------

// NRF52 boards will define this in variant.h
#ifndef RF95_SCK
#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS 18
#endif

#endif

//
// Standard definitions for !ESP32 targets
//

#ifdef NO_ESP32
// Nop definition for these attributes - not used on NRF52
#define EXT_RAM_ATTR
#define IRAM_ATTR
#define RTC_DATA_ATTR
#endif

// -----------------------------------------------------------------------------
// OLED
// -----------------------------------------------------------------------------

#define SSD1306_ADDRESS 0x3C

// The SH1106 controller is almost, but not quite, the same as SSD1306
// Define this if you know you have that controller or your "SSD1306" misbehaves.
//#define USE_SH1106

// Flip the screen upside down by default as it makes more sense on T-BEAM
// devices. Comment this out to not rotate screen 180 degrees.
#define FLIP_SCREEN_VERTICALLY

// DEBUG LED
#define LED_INVERTED 0 // define as 1 if LED is active low (on)

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------

#define GPS_BAUDRATE 9600

#if defined(TBEAM_V10)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "tbeam"

// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 38     // The middle button GPIO on the T-Beam
#define BUTTON_PIN_ALT 13 // Alternate GPIO for an external button if needed

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95
#define USE_SX1262

#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33 // SX1262 IRQ
#define LORA_DIO2 32 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#ifdef USE_SX1262
#define SX1262_CS RF95_NSS // FIXME - we really should define LORA_CS instead
#define SX1262_DIO1 LORA_DIO1
#define SX1262_BUSY LORA_DIO2
#define SX1262_RESET LORA_RESET
#define SX1262_E22 // Not really an E22 but TTGO seems to be trying to clone that
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)
#endif

// Leave undefined to disable our PMU IRQ handler
#define PMU_IRQ 35
#define AXP192_SLAVE_ADDRESS 0x34

#elif defined(TBEAM_V07)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "tbeam0.7"

// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 39
#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33 // Not really used
#define LORA_DIO2 32 // Not really used

// This board has different GPS pins than all other boards
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 12
#define GPS_TX_PIN 15

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V2)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "heltec"

// the default ESP32 Pin of 15 is the Oled SCL, set to 36 and 37 and works fine.
// Tested on Neo6m module.
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 37

#ifndef USE_JTAG  // gpio15 is TDO for JTAG, so no I2C on this board while doing jtag
#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 15
#endif

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN 0   // If defined, this will be used for user button presses

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#ifndef USE_JTAG
#define LORA_RESET 14
#endif
#define LORA_DIO1 35 // Not really used
#define LORA_DIO2 34 // Not really used

#elif defined(TLORA_V1)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "tlora-v1"
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 37

#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 15

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

// #define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 2     // If defined we will blink this LED
#define BUTTON_PIN 0  // If defined, this will be used for user button presses
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 35 // Not really used
#define LORA_DIO2 34 // Not really used

#elif defined(TLORA_V2)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "tlora-v2"

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 13 // per @eugene

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN                                                                                                               \
    0 // If defined, this will be used for user button presses, if your board doesn't have a physical switch, you can wire one
      // between this pin and ground
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 35 // Not really used
#define LORA_DIO2 34 // Not really used

#elif defined(TLORA_V2_1_16)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "tlora-v2-1-1.6"

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 39

#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN                                                                                                               \
    12 // If defined, this will be used for user button presses, if your board doesn't have a physical switch, you can wire one
       // between this pin and ground
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 35 // Not really used
#define LORA_DIO2 34 // Not really used

#endif

#ifdef ARDUINO_NRF52840_PCA10056

// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "nrf52dk"

// This board uses 0 to be mean LED on
#undef LED_INVERTED
#define LED_INVERTED 1

#elif defined(ARDUINO_NRF52840_PPR)

#define HW_VENDOR "ppr"

#elif NRF52_SERIES

#define HW_VENDOR "nrf52unknown" // FIXME - unknown nrf52 board

#elif PORTDUINO

#define HW_VENDOR "portduino"

#define USE_SIM_RADIO

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33 // Not really used
#define LORA_DIO2 32 // Not really used

// Fake SPI device selections
#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS 18

#endif

#ifdef USE_RF95
#define RF95_RESET LORA_RESET
#define RF95_IRQ LORA_DIO0  // on SX1262 version this is a no connect DIO0
#define RF95_DIO1 LORA_DIO1 // Note: not really used for RF95
#define RF95_DIO2 LORA_DIO2 // Note: not really used for RF95
#endif

// -----------------------------------------------------------------------------
// DEBUG
// -----------------------------------------------------------------------------

#ifdef CONSOLE_MAX_BAUD
#define SERIAL_BAUD CONSOLE_MAX_BAUD
#else
#define SERIAL_BAUD 921600 // Serial debug baud rate
#endif

#include "SerialConsole.h"

#define DEBUG_PORT console // Serial debug port

// What platforms should use SEGGER?
#ifdef NRF52_SERIES

// Always include the SEGGER code on NRF52 - because useful for debugging
#include "SEGGER_RTT.h"

// Debug printing to segger console
#define SEGGER_MSG(...) SEGGER_RTT_printf(0, __VA_ARGS__)

// If we are not on a NRF52840 (which has built in USB-ACM serial support) and we don't have serial pins hooked up, then we MUST
// use SEGGER for debug output
#if !defined(PIN_SERIAL_RX) && !defined(NRF52840_XXAA)
// No serial ports on this board - ONLY use segger in memory console
#define USE_SEGGER
#endif

#else
#define SERIAL0_RX_GPIO 3 // Always GPIO3 on ESP32
#endif

#ifdef USE_SEGGER
#define DEBUG_MSG(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#ifdef DEBUG_PORT
#define DEBUG_MSG(...) DEBUG_PORT.printf(__VA_ARGS__)
#else
#define DEBUG_MSG(...)
#endif
#endif

// -----------------------------------------------------------------------------
// AXP192 (Rev1-specific options)
// -----------------------------------------------------------------------------

#define GPS_POWER_CTRL_CH 3
#define LORA_POWER_CTRL_CH 2
