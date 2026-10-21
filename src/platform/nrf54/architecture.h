#pragma once

// The nRF54L port shares the main firmware's nRF52 code paths (Bluefruit BLE, InternalFileSystem, FreeRTOS)
#define ARCH_NRF52
#define ARCH_NRF54L

//
// defaults for the nRF54L architecture
//

// Internal reference 0.6V with gain 1/6: 0..3.6V ADC range unless a variant overrides it
#ifndef AREF_VOLTAGE
#define AREF_VOLTAGE 3.6
#endif

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

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
// No HAS_CUSTOM_CRYPTO_ENGINE: the generic CryptoEngine is used

//
// set HW_VENDOR
//

// No HardwareModel entries exist for nRF54L boards yet
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW

//
// Standard definitions for nRF54L targets
//

#if defined(PIN_LED1) && !defined(LED_POWER)
#define LED_POWER PIN_LED1
#endif

#ifdef PIN_BUTTON1
#define BUTTON_PIN PIN_BUTTON1
#endif

#ifdef PIN_BUTTON_TOUCH
#define BUTTON_PIN_TOUCH PIN_BUTTON_TOUCH
#endif

// printf() in main-nrf54.cpp and the hardfault handler write to the RTT console
#include "SEGGER_RTT.h"

#define SEGGER_STDOUT_CH 0
#define SEGGER_MSG(...) SEGGER_RTT_printf(SEGGER_STDOUT_CH, __VA_ARGS__)

// Detect if running in ISR context (ARM Cortex-M33)
#define xPortInIsrContext() ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0 ? pdFALSE : pdTRUE)
