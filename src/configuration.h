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
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR EXT_RAM_ATTR
#endif

// -----------------------------------------------------------------------------
// Regulatory overrides
// -----------------------------------------------------------------------------

// Override user saved region, for producing region-locked builds
// #define REGULATORY_LORA_REGIONCODE meshtastic_Config_LoRaConfig_RegionCode_SG_923

// Total system gain in dBm to subtract from Tx power to remain within regulatory ERP limit for non-licensed operators
// This value should be set in variant.h and is PA gain + antenna gain (if system ships with an antenna)
#ifndef REGULATORY_GAIN_LORA
#define REGULATORY_GAIN_LORA 0
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
#define INA_ADDR_WAVESHARE_UPS 0x43
#define INA3221_ADDR 0x42
#define MAX1704X_ADDR 0x36
#define QMC6310_ADDR 0x1C
#define QMI8658_ADDR 0x6B
#define QMC5883L_ADDR 0x0D
#define HMC5883L_ADDR 0x1E
#define SHTC3_ADDR 0x70
#define LPS22HB_ADDR 0x5C
#define LPS22HB_ADDR_ALT 0x5D
#define SHT31_4x_ADDR 0x44
#define PMSA0031_ADDR 0x12
#define AHT10_ADDR 0x38
#define RCWL9620_ADDR 0x57
#define VEML7700_ADDR 0x10
#define TSL25911_ADDR 0x29
#define OPT3001_ADDR 0x45
#define OPT3001_ADDR_ALT 0x44
#define MLX90632_ADDR 0x3A
#define DFROBOT_LARK_ADDR 0x42
#define NAU7802_ADDR 0x2A

// -----------------------------------------------------------------------------
// ACCELEROMETER
// -----------------------------------------------------------------------------
#define MPU6050_ADDR 0x68
#define STK8BXX_ADR 0x18
#define LIS3DH_ADR 0x18
#define BMA423_ADDR 0x19
#define LSM6DS3_ADDR 0x6A
#define BMX160_ADDR 0x69
#define ICM20948_ADDR 0x69
#define ICM20948_ADDR_ALT 0x68

// -----------------------------------------------------------------------------
// LED
// -----------------------------------------------------------------------------
#define NCP5623_ADDR 0x38

// -----------------------------------------------------------------------------
// Security
// -----------------------------------------------------------------------------
#define ATECC608B_ADDR 0x35

// -----------------------------------------------------------------------------
// IO Expander
// -----------------------------------------------------------------------------
#define TCA9535_ADDR 0x20
#define TCA9555_ADDR 0x26

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------
#ifndef GPS_THREAD_INTERVAL
#define GPS_THREAD_INTERVAL 200
#endif

// -----------------------------------------------------------------------------
// Touchscreen
// -----------------------------------------------------------------------------
#define FT6336U_ADDR 0x48

// convert 24-bit color to 16-bit (56K)
#define COLOR565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))

/* Step #1: offer chance for variant-specific defines */
#include "variant.h"

#if defined(VEXT_ENABLE) && !defined(VEXT_ON_VALUE)
// Older variant.h files might not be defining this value, so stay with the old default
#define VEXT_ON_VALUE LOW
#endif

#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE 9600
#endif

/* Step #2: follow with defines common to the architecture;
   also enable HAS_ option not specifically disabled by variant.h */
#include "architecture.h"

#ifndef DEFAULT_REBOOT_SECONDS
#define DEFAULT_REBOOT_SECONDS 7
#endif

#ifndef DEFAULT_SHUTDOWN_SECONDS
#define DEFAULT_SHUTDOWN_SECONDS 2
#endif

#ifndef MINIMUM_SAFE_FREE_HEAP
#define MINIMUM_SAFE_FREE_HEAP 1500
#endif

#ifndef WIRE_INTERFACES_COUNT
// Officially an NRF52 macro
// Repurposed cross-platform to identify devices using Wire1
#if defined(I2C_SDA1) || defined(PIN_WIRE1_SDA)
#define WIRE_INTERFACES_COUNT 2
#elif HAS_WIRE
#define WIRE_INTERFACES_COUNT 1
#endif
#endif

/* Step #3: mop up with disabled values for HAS_ options not handled by the above two */

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

#ifndef HW_VENDOR
#error HW_VENDOR must be defined
#endif

// -----------------------------------------------------------------------------
// Global switches to turn off features for a minimized build
// -----------------------------------------------------------------------------

// #define MESHTASTIC_MINIMIZE_BUILD 1
#ifdef MESHTASTIC_MINIMIZE_BUILD
#define MESHTASTIC_EXCLUDE_MODULES 1
#define MESHTASTIC_EXCLUDE_WIFI 1
#define MESHTASTIC_EXCLUDE_BLUETOOTH 1
#define MESHTASTIC_EXCLUDE_GPS 1
#define MESHTASTIC_EXCLUDE_SCREEN 1
#define MESHTASTIC_EXCLUDE_MQTT 1
#define MESHTASTIC_EXCLUDE_POWERMON 1
#define MESHTASTIC_EXCLUDE_I2C 1
#define MESHTASTIC_EXCLUDE_PKI 1
#define MESHTASTIC_EXCLUDE_POWER_FSM 1
#define MESHTASTIC_EXCLUDE_TZ 1
#endif

// Turn off all optional modules
#ifdef MESHTASTIC_EXCLUDE_MODULES
#define MESHTASTIC_EXCLUDE_AUDIO 1
#define MESHTASTIC_EXCLUDE_DETECTIONSENSOR 1
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 1
#define MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION 1
#define MESHTASTIC_EXCLUDE_PAXCOUNTER 1
#define MESHTASTIC_EXCLUDE_POWER_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_RANGETEST 1
#define MESHTASTIC_EXCLUDE_REMOTEHARDWARE 1
#define MESHTASTIC_EXCLUDE_STOREFORWARD 1
#define MESHTASTIC_EXCLUDE_TEXTMESSAGE 1
#define MESHTASTIC_EXCLUDE_ATAK 1
#define MESHTASTIC_EXCLUDE_CANNEDMESSAGES 1
#define MESHTASTIC_EXCLUDE_NEIGHBORINFO 1
#define MESHTASTIC_EXCLUDE_TRACEROUTE 1
#define MESHTASTIC_EXCLUDE_WAYPOINT 1
#define MESHTASTIC_EXCLUDE_INPUTBROKER 1
#define MESHTASTIC_EXCLUDE_SERIAL 1
#define MESHTASTIC_EXCLUDE_POWERSTRESS 1
#define MESHTASTIC_EXCLUDE_ADMIN 1
#endif

// // Turn off wifi even if HW supports wifi (webserver relies on wifi and is also disabled)
#ifdef MESHTASTIC_EXCLUDE_WIFI
#define MESHTASTIC_EXCLUDE_WEBSERVER 1
#undef HAS_WIFI
#define HAS_WIFI 0
#endif

// Allow code that needs internet to just check HAS_NETWORKING rather than HAS_WIFI || HAS_ETHERNET
#define HAS_NETWORKING (HAS_WIFI || HAS_ETHERNET)

// // Turn off Bluetooth
#ifdef MESHTASTIC_EXCLUDE_BLUETOOTH
#undef HAS_BLUETOOTH
#define HAS_BLUETOOTH 0
#endif

// // Turn off GPS
#ifdef MESHTASTIC_EXCLUDE_GPS
#undef HAS_GPS
#define HAS_GPS 0
#undef MESHTASTIC_EXCLUDE_RANGETEST
#define MESHTASTIC_EXCLUDE_RANGETEST 1
#endif

// Turn off Screen
#ifdef MESHTASTIC_EXCLUDE_SCREEN
#undef HAS_SCREEN
#define HAS_SCREEN 0
#endif

#include "DebugConfiguration.h"
#include "RF95Configuration.h"
