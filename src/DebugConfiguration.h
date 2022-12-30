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

#define MESHTASTIC_LOG_LEVEL_DEBUG "DEBUG"
#define MESHTASTIC_LOG_LEVEL_INFO "INFO "
#define MESHTASTIC_LOG_LEVEL_WARN "WARN "
#define MESHTASTIC_LOG_LEVEL_ERROR "ERROR"
#define MESHTASTIC_LOG_LEVEL_TRACE "TRACE"

#include "SerialConsole.h"

#define DEBUG_PORT (*console) // Serial debug port

#ifdef USE_SEGGER
#define LOG_DEBUG(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_INFO(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_WARN(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_ERROR(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#ifdef DEBUG_PORT
#define LOG_DEBUG(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_TRACE(...) DEBUG_PORT.log(MESHTASTIC_LOG_TRACE, __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#endif
#endif

// -----------------------------------------------------------------------------
// AXP192 (Rev1-specific options)
// -----------------------------------------------------------------------------

#define GPS_POWER_CTRL_CH 3
#define LORA_POWER_CTRL_CH 2

// Default Bluetooth PIN
#define defaultBLEPin 123456
