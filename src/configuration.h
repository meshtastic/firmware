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

#elif defined(RAK_11200)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_RAK11200

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V2)

#ifdef HELTEC_V2_0
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_HELTEC_V2_0

#endif

#ifdef HELTEC_V2_1
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_HELTEC_V2_1

#endif

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32)

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

#elif defined(PRIVATE_HW)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_PRIVATE_HW

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

#endif

#include "variant.h"
#include "RF95Configuration.h"
#include "DebugConfiguration.h"