#pragma once

#include "configuration.h"

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
#define MESHTASTIC_LOG_LEVEL_CRIT "CRIT "
#define MESHTASTIC_LOG_LEVEL_TRACE "TRACE"

#include "SerialConsole.h"

// If defined we will include support for ARM ICE "semihosting" for a virtual
// console over the JTAG port (to replace the normal serial port)
// Note: Normally this flag is passed into the gcc commandline by platformio.ini.
// for an example see env:rak4631_dap.
// #ifndef USE_SEMIHOSTING
// #define USE_SEMIHOSTING
// #endif

#define DEBUG_PORT (*console) // Serial debug port

#ifdef USE_SEGGER
// #undef DEBUG_PORT
#define LOG_DEBUG(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_INFO(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_WARN(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_ERROR(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_CRIT(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#define LOG_TRACE(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
#define LOG_DEBUG(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_CRIT(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_CRIT, __VA_ARGS__)
#define LOG_TRACE(...) DEBUG_PORT.log(MESHTASTIC_LOG_LEVEL_TRACE, __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#define LOG_CRIT(...)
#define LOG_TRACE(...)
#endif
#endif

#define SYSLOG_NILVALUE "-"

#define SYSLOG_CRIT 2  /* critical conditions */
#define SYSLOG_ERR 3   /* error conditions */
#define SYSLOG_WARN 4  /* warning conditions */
#define SYSLOG_INFO 6  /* informational */
#define SYSLOG_DEBUG 7 /* debug-level messages */
// trace does not go out to syslog (yet?)

#define LOG_PRIMASK 0x07 /* mask to extract priority part (internal) */
                         /* extract priority */
#define LOG_PRI(p) ((p)&LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri) (((fac) << 3) | (pri))

/* facility codes */
#define LOGLEVEL_KERN (0 << 3)      /* kernel messages */
#define LOGLEVEL_USER (1 << 3)      /* random user-level messages */
#define LOGLEVEL_MAIL (2 << 3)      /* mail system */
#define LOGLEVEL_DAEMON (3 << 3)    /* system daemons */
#define LOGLEVEL_AUTH (4 << 3)      /* security/authorization messages */
#define LOGLEVEL_SYSLOG (5 << 3)    /* messages generated internally by syslogd */
#define LOGLEVEL_LPR (6 << 3)       /* line printer subsystem */
#define LOGLEVEL_NEWS (7 << 3)      /* network news subsystem */
#define LOGLEVEL_UUCP (8 << 3)      /* UUCP subsystem */
#define LOGLEVEL_CRON (9 << 3)      /* clock daemon */
#define LOGLEVEL_AUTHPRIV (10 << 3) /* security/authorization messages (private) */
#define LOGLEVEL_FTP (11 << 3)      /* ftp daemon */

/* other codes through 15 reserved for system use */
#define LOGLEVEL_LOCAL0 (16 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL1 (17 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL2 (18 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL3 (19 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL4 (20 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL5 (21 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL6 (22 << 3) /* reserved for local use */
#define LOGLEVEL_LOCAL7 (23 << 3) /* reserved for local use */

#define LOG_NFACILITIES 24 /* current number of facilities */
#define LOG_FACMASK 0x03f8 /* mask to extract facility part */
                           /* facility of pri */
#define LOG_FAC(p) (((p)&LOG_FACMASK) >> 3)

#define LOG_MASK(pri) (1 << (pri))             /* mask for one priority */
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1) /* all priorities through pri */

// -----------------------------------------------------------------------------
// AXP192 (Rev1-specific options)
// -----------------------------------------------------------------------------

#define GPS_POWER_CTRL_CH 3
#define LORA_POWER_CTRL_CH 2

// Default Bluetooth PIN
#define defaultBLEPin 123456

#if HAS_ETHERNET
#include <RAK13800_W5100S.h>
#endif // HAS_ETHERNET

#if HAS_WIFI
#include <WiFi.h>
#endif // HAS_WIFI

#if HAS_NETWORKING

class Syslog
{
  private:
    UDP *_client;
    IPAddress _ip;
    const char *_server;
    uint16_t _port;
    const char *_deviceHostname;
    const char *_appName;
    uint16_t _priDefault;
    uint8_t _priMask = 0xff;
    bool _enabled = false;

    bool _sendLog(uint16_t pri, const char *appName, const char *message);

  public:
    explicit Syslog(UDP &client);

    Syslog &server(const char *server, uint16_t port);
    Syslog &server(IPAddress ip, uint16_t port);
    Syslog &deviceHostname(const char *deviceHostname);
    Syslog &appName(const char *appName);
    Syslog &defaultPriority(uint16_t pri = LOGLEVEL_KERN);
    Syslog &logMask(uint8_t priMask);

    void enable();
    void disable();
    bool isEnabled();

    bool vlogf(uint16_t pri, const char *fmt, va_list args) __attribute__((format(printf, 3, 0)));
    bool vlogf(uint16_t pri, const char *appName, const char *fmt, va_list args) __attribute__((format(printf, 3, 0)));
};

#endif // HAS_ETHERNET || HAS_WIFI