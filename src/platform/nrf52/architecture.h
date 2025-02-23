#pragma once

#define ARCH_NRF52

//
// defaults for NRF52 architecture
//
#ifndef HAS_BLUETOOTH
#define HAS_BLUETOOTH 1
#endif
#ifndef HAS_SCREEN
#define HAS_SCREEN 1
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 1
#endif
#ifndef HAS_GPS
#define HAS_GPS 1
#endif
#ifndef HAS_BUTTON
#define HAS_BUTTON 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 1
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 1
#endif
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
#define HAS_CUSTOM_CRYPTO_ENGINE 1
#endif

//
// set HW_VENDOR
//

// This string must exactly match the case used in release file names or the android updater won't work
#ifdef ARDUINO_NRF52840_PCA10056
#define HW_VENDOR meshtastic_HardwareModel_NRF52840DK
#elif defined(ARDUINO_NRF52840_PPR)
#define HW_VENDOR meshtastic_HardwareModel_PPR
#elif defined(RAK2560)
#define HW_VENDOR meshtastic_HardwareModel_RAK2560
#elif defined(WISMESH_TAP)
#define HW_VENDOR meshtastic_HardwareModel_WISMESH_TAP
#elif defined(RAK4630)
#define HW_VENDOR meshtastic_HardwareModel_RAK4631
#elif defined(TTGO_T_ECHO)
#define HW_VENDOR meshtastic_HardwareModel_T_ECHO
#elif defined(NANO_G2_ULTRA)
#define HW_VENDOR meshtastic_HardwareModel_NANO_G2_ULTRA
#elif defined(CANARYONE)
#define HW_VENDOR meshtastic_HardwareModel_CANARYONE
#elif defined(NORDIC_PCA10059)
#define HW_VENDOR meshtastic_HardwareModel_NRF52840_PCA10059
#elif defined(TWC_MESH_V4)
#define HW_VENDOR meshtastic_HardwareModel_TWC_MESH_V4
#elif defined(NRF52_PROMICRO_DIY)
#define HW_VENDOR meshtastic_HardwareModel_NRF52_PROMICRO_DIY
#elif defined(WIO_WM1110)
#define HW_VENDOR meshtastic_HardwareModel_WIO_WM1110
#elif defined(TRACKER_T1000_E)
#define HW_VENDOR meshtastic_HardwareModel_TRACKER_T1000_E
#elif defined(ME25LS01_4Y10TD)
#define HW_VENDOR meshtastic_HardwareModel_ME25LS01_4Y10TD
#elif defined(MS24SF1)
#define HW_VENDOR meshtastic_HardwareModel_MS24SF1
#elif defined(PRIVATE_HW) || defined(FEATHER_DIY)
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#elif defined(HELTEC_T114)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_MESH_NODE_T114
#else
#define HW_VENDOR meshtastic_HardwareModel_NRF52_UNKNOWN
#endif

//
// Standard definitions for NRF52 targets
//

#ifdef ARDUINO_NRF52840_PCA10056

// This board uses 0 to be mean LED on
#undef LED_STATE_ON
#define LED_STATE_ON 0 // State when LED is lit

#endif

#ifdef _SEEED_XIAO_NRF52840_SENSE_H_

// This board uses 0 to be mean LED on
#undef LED_STATE_ON
#define LED_STATE_ON 0 // State when LED is lit

#endif

#define LED_PIN PIN_LED1 // LED1 on nrf52840-DK

#ifdef PIN_BUTTON1
#define BUTTON_PIN PIN_BUTTON1
#endif

#ifdef PIN_BUTTON2
#define BUTTON_PIN_ALT PIN_BUTTON2
#endif

#ifdef PIN_BUTTON_TOUCH
#define BUTTON_PIN_TOUCH PIN_BUTTON_TOUCH
#endif

// Always include the SEGGER code on NRF52 - because useful for debugging
#include "SEGGER_RTT.h"

// The channel we send stdout data to
#define SEGGER_STDOUT_CH 0

// Debug printing to segger console
#define SEGGER_MSG(...) SEGGER_RTT_printf(SEGGER_STDOUT_CH, __VA_ARGS__)

// If we are not on a NRF52840 (which has built in USB-ACM serial support) and we don't have serial pins hooked up, then we MUST
// use SEGGER for debug output
#if !defined(PIN_SERIAL_RX) && !defined(NRF52840_XXAA)
// No serial ports on this board - ONLY use segger in memory console
#define USE_SEGGER
#endif
