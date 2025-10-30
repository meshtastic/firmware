#pragma once

// Minimal stub configuration.h for native testing
// Only defines what's needed for LSM library to compile

#include "Arduino.h"
#include <cstdio>

// Minimal logging macros for tests
#define LOG_DEBUG(...)                                                                                                           \
    printf("[DEBUG] " __VA_ARGS__);                                                                                              \
    printf("\n")
#define LOG_INFO(...)                                                                                                            \
    printf("[INFO] " __VA_ARGS__);                                                                                               \
    printf("\n")
#define LOG_WARN(...)                                                                                                            \
    printf("[WARN] " __VA_ARGS__);                                                                                               \
    printf("\n")
#define LOG_ERROR(...)                                                                                                           \
    printf("[ERROR] " __VA_ARGS__);                                                                                              \
    printf("\n")
#define LOG_CRIT(...)                                                                                                            \
    printf("[CRIT] " __VA_ARGS__);                                                                                               \
    printf("\n")
#define LOG_TRACE(...)                                                                                                           \
    printf("[TRACE] " __VA_ARGS__);                                                                                              \
    printf("\n")

// Minimal required defines (set defaults, can be overridden in Makefile)
#ifndef APP_VERSION
#define APP_VERSION "test-1.0.0"
#endif

#ifndef HW_VERSION
#define HW_VERSION "1.0"
#endif

// Minimal feature flags - assume everything is disabled for tests
#ifndef HAS_WIFI
#define HAS_WIFI 0
#endif

#ifndef HAS_ETHERNET
#define HAS_ETHERNET 0
#endif

#ifndef HAS_SCREEN
#define HAS_SCREEN 0
#endif

#ifndef HAS_TFT
#define HAS_TFT 0
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
#define HW_VENDOR "test"
#endif

// Disable all optional modules for tests
#define MESHTASTIC_EXCLUDE_MODULES 1
#define MESHTASTIC_EXCLUDE_WIFI 1
#define MESHTASTIC_EXCLUDE_BLUETOOTH 1
#define MESHTASTIC_EXCLUDE_GPS 1
#define MESHTASTIC_EXCLUDE_SCREEN 1

#ifndef WIRE_INTERFACES_COUNT
#define WIRE_INTERFACES_COUNT 1
#endif

// Empty variant.h stub (just in case)
#ifndef _VARIANT_H_
#define _VARIANT_H_
#endif
