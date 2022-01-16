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
#error APP_VERSION must be set by the build environment
#endif

// If app version is not specified we assume we are not being invoked by the build script
#ifndef HW_VERSION
#error HW_VERSION, and HW_VERSION_countryname must be set by the build environment
#endif

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// If we are using the JTAG port for debugging, some pins must be left free for that (and things like GPS have to be disabled)
// we don't support jtag on the ttgo - access to gpio 12 is a PITA
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

#ifdef PIN_BUTTON2
#define BUTTON_PIN_ALT PIN_BUTTON2
#endif

#ifdef PIN_BUTTON_TOUCH
#define BUTTON_PIN_TOUCH PIN_BUTTON_TOUCH
#endif

#else

//
// Standard definitions for ESP32 targets
//

#define HAS_WIFI

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
#define ST7567_ADDRESS 0x3F

// The SH1106 controller is almost, but not quite, the same as SSD1306
// Define this if you know you have that controller or your "SSD1306" misbehaves.
//#define USE_SH1106

// Flip the screen upside down by default as it makes more sense on T-BEAM
// devices. Comment this out to not rotate screen 180 degrees.
#define SCREEN_FLIP_VERTICALLY

// Define if screen should be mirrored left to right
// #define SCREEN_MIRROR

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------

#define GPS_BAUDRATE 9600

#if defined(TBEAM_V10)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TBEAM

#elif defined(TBEAM_V07)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TBEAM0p7

#elif defined(DIY_V1)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_DIY_V1

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V2)

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

// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
#define ADC_MULTIPLIER 3.2

#ifdef HELTEC_V2_0
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_HELTEC_V2_0

#endif

#ifdef HELTEC_V2_1
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_HELTEC_V2_1

#endif

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32)

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

#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN 0   // If defined, this will be used for user button presses

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#ifndef USE_JTAG
#define LORA_RESET 14
#endif
#define LORA_DIO1 33 // Not really used
#define LORA_DIO2 32 // Not really used

// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
#define ADC_MULTIPLIER 3.2

#define HW_VENDOR HardwareModel_HELTEC_V1

#elif defined(TLORA_V1)

#define HW_VENDOR HardwareModel_TLORA_V1

#elif defined(TLORA_V2)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TLORA_V2

#elif defined(TLORA_V1_3)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TLORA_V1_1p3

#elif defined(TLORA_V2_1_16)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TLORA_V2_1_1p6

#elif defined(GENIEBLOCKS)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_GENIEBLOCKS

#endif

#ifdef ARDUINO_NRF52840_PCA10056

// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_NRF52840DK

// This board uses 0 to be mean LED on
#undef LED_INVERTED
#define LED_INVERTED 1

#elif defined(ARDUINO_NRF52840_PPR)

#define HW_VENDOR HardwareModel_PPR

#elif defined(RAK4630)

#define HW_VENDOR HardwareModel_RAK4631

#elif defined(TTGO_T_ECHO)

#define HW_VENDOR HardwareModel_T_ECHO

#elif NRF52_SERIES

#define HW_VENDOR HardwareModel_NRF52_UNKNOWN

#elif PORTDUINO

#define HW_VENDOR HardwareModel_PORTDUINO

#define USE_SIM_RADIO

// Pine64 uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262.  Currently the RF95 code is disabled because I think the RF95 module won't need to ship.
// #define USE_RF95
#define USE_SX1262

// Fake SPI device selections
#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS RADIOLIB_NC // the ch341f spi controller does CS for us

#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 33 // SX1262 IRQ, called DIO0 on pinelora schematic, pin 7 on ch341f "ack" - FIXME, enable hwints in linux
#define LORA_DIO2 32 // SX1262 BUSY, actually connected to "DIO5" on pinelora schematic, pin 8 on ch341f "slct" 
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#ifdef USE_SX1262
#define SX126X_CS 20 // CS0 on pinelora schematic, hooked to gpio D0 on ch341f
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
// HOPE RFM90 does not have a TCXO therefore not SX126X_E22 
#endif

#endif

#include "DebugConfiguration.h"