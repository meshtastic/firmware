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
