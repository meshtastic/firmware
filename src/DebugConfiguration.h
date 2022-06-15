// DEBUG LED
#ifndef LED_INVERTED
#define LED_INVERTED 0 // define as 1 if LED is active low (on)
#endif

// -----------------------------------------------------------------------------
// DEBUG
// -----------------------------------------------------------------------------

#ifdef CONSOLE_MAX_BAUD
#define SERIAL_BAUD CONSOLE_MAX_BAUD
#else
#define SERIAL_BAUD 115200 // Serial debug baud rate
#endif

#include "SerialConsole.h"

#define DEBUG_PORT (*console) // Serial debug port

// What platforms should use SEGGER?
#ifdef NRF52_SERIES

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

#else
#define SERIAL0_RX_GPIO 3 // Always GPIO3 on ESP32
#endif

#ifdef USE_SEGGER
#define DEBUG_MSG(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#ifdef DEBUG_PORT
#define DEBUG_MSG(...) DEBUG_PORT.logDebug(__VA_ARGS__)
#else
#define DEBUG_MSG(...)
#endif
#endif

// -----------------------------------------------------------------------------
// AXP192 (Rev1-specific options)
// -----------------------------------------------------------------------------

#define GPS_POWER_CTRL_CH 3
#define LORA_POWER_CTRL_CH 2

// Default Bluetooth PIN
#define defaultBLEPin 123456
