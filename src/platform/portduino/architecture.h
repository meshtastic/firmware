#pragma once

#define ARCH_PORTDUINO 1

//
// set HW_VENDOR
//

#define HW_VENDOR portduino_status.hardwareModel

#ifndef HAS_BUTTON
#define HAS_BUTTON 1
#endif
#ifndef HAS_WIFI
#define HAS_WIFI 1
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 1
#endif
#ifndef HAS_TRACKBALL
#define HAS_TRACKBALL 1
#define TB_DOWN (uint8_t) portduino_config.tbDownPin.pin
#define TB_UP (uint8_t) portduino_config.tbUpPin.pin
#define TB_LEFT (uint8_t) portduino_config.tbLeftPin.pin
#define TB_RIGHT (uint8_t) portduino_config.tbRightPin.pin
#define TB_PRESS (uint8_t) portduino_config.tbPressPin.pin
#endif

// HUB75 RGB-matrix panel support on Raspberry Pi (Portduino) turns on automatically when the
// hzeller/rpi-rgb-led-matrix library is installed - its headers land on the include path via
// `pkg-config rgbmatrix` (wired up in variants/native/portduino/platformio.ini). When absent,
// HAS_HUB75_NATIVE stays undefined and the backend compiles out. See src/graphics/HUB75Display.cpp.
#if defined(ARCH_PORTDUINO) && __has_include(<led-matrix.h>)
#define HAS_HUB75_NATIVE 1
#endif

// BLE peripheral support (BlueZ over D-Bus) on Linux turns on automatically
// when the sdbus-c++ dev package is installed (wired up in
// variants/native/portduino.ini). When absent, HAS_BLUETOOTH stays at
// configuration.h's default of 0 and the whole backend compiles out. Runtime
// activation additionally requires `Bluetooth: Enabled: true` in config.yaml.
// See src/platform/portduino/LinuxBluetooth.cpp.
#if defined(PORTDUINO_LINUX_HARDWARE) && __has_include(<sdbus-c++/sdbus-c++.h>)
#define HAS_BLUETOOTH 1
#endif