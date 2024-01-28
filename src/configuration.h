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

#ifdef RV3028_RTC
#include "Melopero_RV3028.h"
#endif
#ifdef PCF8563_RTC
#include "pcf8563.h"
#endif

// -----------------------------------------------------------------------------
// Version
// -----------------------------------------------------------------------------

// If app version is not specified we assume we are not being invoked by the build script
#ifndef APP_VERSION
#error APP_VERSION must be set by the build environment
#endif

// FIXME: This is still needed by the Bluetooth Stack and needs to be replaced by something better. Remnant of the old versioning
// system.
#ifndef HW_VERSION
#define HW_VERSION "1.0"
#endif

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// If we are using the JTAG port for debugging, some pins must be left free for that (and things like GPS have to be disabled)
// we don't support jtag on the ttgo - access to gpio 12 is a PITA
#define REQUIRE_RADIO true // If true, we will fail to start if the radio is not found

/// Convert a preprocessor name into a quoted string
#define xstr(s) ystr(s)
#define ystr(s) #s

/// Convert a preprocessor name into a quoted string and if that string is empty use "unset"
#define optstr(s) (xstr(s)[0] ? xstr(s) : "unset")

// Nop definition for these attributes that are specific to ESP32
#ifndef EXT_RAM_ATTR
#define EXT_RAM_ATTR
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

// -----------------------------------------------------------------------------
// Feature toggles
// -----------------------------------------------------------------------------

// Disable use of the NTP library and related features
// #define DISABLE_NTP

// Disable the welcome screen and allow
// #define DISABLE_WELCOME_UNSET

// -----------------------------------------------------------------------------
// OLED & Input
// -----------------------------------------------------------------------------

#define SSD1306_ADDRESS 0x3C
#define ST7567_ADDRESS 0x3F

// The SH1106 controller is almost, but not quite, the same as SSD1306
// Define this if you know you have that controller or your "SSD1306" misbehaves.
// #define USE_SH1106

// Define if screen should be mirrored left to right
// #define SCREEN_MIRROR

// I2C Keyboards (M5Stack, RAK14004, T-Deck)
#define CARDKB_ADDR 0x5F
#define TDECK_KB_ADDR 0x55
#define BBQ10_KB_ADDR 0x1F

// -----------------------------------------------------------------------------
// SENSOR
// -----------------------------------------------------------------------------
#define BME_ADDR 0x76
#define BME_ADDR_ALTERNATE 0x77
#define MCP9808_ADDR 0x18
#define INA_ADDR 0x40
#define INA_ADDR_ALTERNATE 0x41
#define INA3221_ADDR 0x42
#define QMC6310_ADDR 0x1C
#define QMI8658_ADDR 0x6B
#define QMC5883L_ADDR 0x1E
#define SHTC3_ADDR 0x70
#define LPS22HB_ADDR 0x5C
#define LPS22HB_ADDR_ALT 0x5D
#define SHT31_ADDR 0x44
#define PMSA0031_ADDR 0x12

// -----------------------------------------------------------------------------
// ACCELEROMETER
// -----------------------------------------------------------------------------
#define MPU6050_ADDR 0x68
#define LIS3DH_ADR 0x18
#define BMA423_ADDR 0x19

// -----------------------------------------------------------------------------
// LED
// -----------------------------------------------------------------------------
#define NCP5623_ADDR 0x38

// -----------------------------------------------------------------------------
// Security
// -----------------------------------------------------------------------------

#define ATECC608B_ADDR 0x35

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------
#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE 9600
#endif

#ifndef GPS_THREAD_INTERVAL
#define GPS_THREAD_INTERVAL 200
#endif

// convert 24-bit color to 16-bit (56K)
#define COLOR565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))

/* Step #1: offer chance for variant-specific defines */
#include "variant.h"

/* Step #2: follow with defines common to the architecture;
   also enable HAS_ option not specifically disabled by variant.h */
#include "architecture.h"

/* Step #3: mop up with disabled values for HAS_ options not handled by the above two */

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------

#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE 9600
#endif
#ifndef GPS_THREAD_INTERVAL
#define GPS_THREAD_INTERVAL 100
#endif

#ifndef HAS_WIFI
#define HAS_WIFI 0
#endif
#ifndef HAS_ETHERNET
#define HAS_ETHERNET 0
#endif
#ifndef HAS_SCREEN
#define HAS_SCREEN 0
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 0
#endif
#ifndef HAS_GPS
#define HAS_GPS 0
#endif
#ifndef HAS_BUTTON
#define HAS_BUTTON 0
#endif
#ifndef HAS_TRACKBALL
#define HAS_TRACKBALL 0
#endif
#ifndef HAS_TOUCHSCREEN
#define HAS_TOUCHSCREEN 0
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 0
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 0
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 0
#endif
#ifndef HAS_RTC
#define HAS_RTC 0
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 0
#endif
#ifndef HAS_BLUETOOTH
#define HAS_BLUETOOTH 0
#endif

#include "DebugConfiguration.h"
#include "RF95Configuration.h"

#ifndef HW_VENDOR
#error HW_VENDOR must be defined
#endif