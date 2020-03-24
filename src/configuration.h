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
#define APP_VERSION 0.0.0   // this def normally comes from build-all.sh
#define HW_VERSION 1.0 - US // normally comes from build-all.sh and contains the region code
#endif

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// Select which board is being used. If the outside build environment has sent a choice, just use that
#if !defined(T_BEAM_V10) && !defined(HELTEC_LORA32)
#define T_BEAM_V10 // AKA Rev1 (second board released)
// #define HELTEC_LORA32

#define HW_VERSION_US // We encode the hardware freq range in the hw version string, so sw update can eventually install the
                      // correct build
#endif

// If we are using the JTAG port for debugging, some pins must be left free for that (and things like GPS have to be disabled)
// we don't support jtag on the ttgo - access to gpio 12 is a PITA
#ifdef HELTEC_LORA32
//#define USE_JTAG
#endif

#define DEBUG_PORT Serial  // Serial debug port
#define SERIAL_BAUD 115200 // Serial debug baud rate

#define REQUIRE_RADIO true // If true, we will fail to start if the radio is not found

#define xstr(s) str(s)
#define str(s) #s

// -----------------------------------------------------------------------------
// DEBUG
// -----------------------------------------------------------------------------

#ifdef DEBUG_PORT
#define DEBUG_MSG(...) DEBUG_PORT.printf(__VA_ARGS__)
#else
#define DEBUG_MSG(...)
#endif

// -----------------------------------------------------------------------------
// OLED
// -----------------------------------------------------------------------------

#define SSD1306_ADDRESS 0x3C

// Flip the screen upside down by default as it makes more sense on T-BEAM
// devices. Comment this out to not rotate screen 180 degrees.
#define FLIP_SCREEN_VERTICALLY

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------

#define GPS_SERIAL_NUM 1
#define GPS_BAUDRATE 9600

#define GPS_RX_PIN 34
#ifdef USE_JTAG
#define GPS_TX_PIN -1
#else
#define GPS_TX_PIN 12
#endif

// -----------------------------------------------------------------------------
// LoRa SPI
// -----------------------------------------------------------------------------

#define SCK_GPIO 5
#define MISO_GPIO 19
#define MOSI_GPIO 27
#define NSS_GPIO 18

#if defined(T_BEAM_V10)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "TBEAM"

// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 38

#ifndef USE_JTAG
#define RESET_GPIO 14
#endif
#define DIO0_GPIO 26
#define DIO1_GPIO 33 // Note: not really used on this board
#define DIO2_GPIO 32 // Note: not really used on this board

// Leave undefined to disable our PMU IRQ handler
#define PMU_IRQ 35

#elif defined(HELTEC_LORA32)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR "HELTEC"

#ifndef USE_JTAG // gpio15 is TDO for JTAG, so no I2C on this board while doing jtag
#define I2C_SDA 4
#define I2C_SCL 15
#endif

#define RESET_OLED 16

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 25
#define BUTTON_PIN 0

#ifndef USE_JTAG
#define RESET_GPIO 14
#endif
#define DIO0_GPIO 26
#define DIO1_GPIO 35
#define DIO2_GPIO 34
#endif

// -----------------------------------------------------------------------------
// AXP192 (Rev1-specific options)
// -----------------------------------------------------------------------------

// #define AXP192_SLAVE_ADDRESS  0x34 // Now defined in axp20x.h
#define GPS_POWER_CTRL_CH 3
#define LORA_POWER_CTRL_CH 2
