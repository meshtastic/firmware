#include "RedirectablePrint.h"
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <assert.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/time.h>
#include <time.h>

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

#if HAS_NETWORKING
extern Syslog syslog;
#endif
void RedirectablePrint::rpInit()
{
#ifdef HAS_FREE_RTOS
    inDebugPrint = xSemaphoreCreateMutexStatic(&this->_MutexStorageSpace);
#endif
}

void RedirectablePrint::setDestination(Print *_dest)
{
    assert(_dest);
    dest = _dest;
}

size_t RedirectablePrint::write(uint8_t c)
{
    // Always send the characters to our segger JTAG debugger
#ifdef USE_SEGGER
    SEGGER_RTT_PutChar(SEGGER_STDOUT_CH, c);
#endif
    // Account for legacy config transition
    bool serialEnabled = config.has_security ? config.security.serial_enabled : config.device.serial_enabled;
    if (!config.has_lora || serialEnabled)
        dest->write(c);

    return 1; // We always claim one was written, rather than trusting what the
              // serial port said (which could be zero)
}

size_t RedirectablePrint::vprintf(const char *logLevel, const char *format, va_list arg)
{
    va_list copy;
#if ENABLE_JSON_LOGGING || ARCH_PORTDUINO
    static char printBuf[512];
#else
    static char printBuf[160];
#endif

#ifdef ARCH_PORTDUINO
    bool color = !settingsMap[ascii_logs];
#else
    bool color = true;
#endif

    va_copy(copy, arg);
    size_t len = vsnprintf(printBuf, sizeof(printBuf), format, copy);
    va_end(copy);

    // If the resulting string is longer than sizeof(printBuf)-1 characters, the remaining characters are still counted for the
    // return value

    if (len > sizeof(printBuf) - 1) {
        len = sizeof(printBuf) - 1;
        printBuf[sizeof(printBuf) - 2] = '\n';
    }
    for (size_t f = 0; f < len; f++) {
        if (!std::isprint(static_cast<unsigned char>(printBuf[f])) && printBuf[f] != '\n')
            printBuf[f] = '#';
    }
    if (color && logLevel != nullptr) {
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_DEBUG) == 0)
            Print::write("\u001b[34m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_INFO) == 0)
            Print::write("\u001b[32m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_WARN) == 0)
            Print::write("\u001b[33m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_ERROR) == 0)
            Print::write("\u001b[31m", 6);
    }
    len = Print::write(printBuf, len);
    if (color && logLevel != nullptr) {
        Print::write("\u001b[0m", 5);
    }
    return len;
}

void RedirectablePrint::log_to_serial(const char *logLevel, const char *format, va_list arg)
{
    size_t r = 0;

#ifdef ARCH_PORTDUINO
    bool color = !settingsMap[ascii_logs];
#else
    bool color = true;
#endif

    // include the header
    if (color) {
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_DEBUG) == 0)
            Print::write("\u001b[34m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_INFO) == 0)
            Print::write("\u001b[32m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_WARN) == 0)
            Print::write("\u001b[33m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_ERROR) == 0)
            Print::write("\u001b[31m", 6);
        if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_TRACE) == 0)
            Print::write("\u001b[35m", 6);
    }

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // display local time on logfile
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN
#ifdef ARCH_PORTDUINO
        ::printf("%s ", logLevel);
        if (color) {
            ::printf("\u001b[0m");
        }
        ::printf("| %02d:%02d:%02d %u ", hour, min, sec, millis() / 1000);
#else
        printf("%s ", logLevel);
        if (color) {
            printf("\u001b[0m");
        }
        printf("| %02d:%02d:%02d %u ", hour, min, sec, millis() / 1000);
#endif
    } else {
#ifdef ARCH_PORTDUINO
        ::printf("%s ", logLevel);
        if (color) {
            ::printf("\u001b[0m");
        }
        ::printf("| ??:??:?? %u ", millis() / 1000);
#else
        printf("%s ", logLevel);
        if (color) {
            printf("\u001b[0m");
        }
        printf("| ??:??:?? %u ", millis() / 1000);
#endif
    }
    auto thread = concurrency::OSThread::currentThread;
    if (thread) {
        print("[");
        // printf("%p ", thread);
        // assert(thread->ThreadName.length());
        print(thread->ThreadName);
        print("] ");
    }
    r += vprintf(logLevel, format, arg);
}

void RedirectablePrint::log_to_syslog(const char *logLevel, const char *format, va_list arg)
{
#if HAS_NETWORKING && !defined(ARCH_PORTDUINO)
    // if syslog is in use, collect the log messages and send them to syslog
    if (syslog.isEnabled()) {
        int ll = 0;
        switch (logLevel[0]) {
        case 'D':
            ll = SYSLOG_DEBUG;
            break;
        case 'I':
            ll = SYSLOG_INFO;
            break;
        case 'W':
            ll = SYSLOG_WARN;
            break;
        case 'E':
            ll = SYSLOG_ERR;
            break;
        case 'C':
            ll = SYSLOG_CRIT;
            break;
        default:
            ll = 0;
        }
        auto thread = concurrency::OSThread::currentThread;
        if (thread) {
            syslog.vlogf(ll, thread->ThreadName.c_str(), format, arg);
        } else {
            syslog.vlogf(ll, format, arg);
        }
    }
#endif
}

void RedirectablePrint::log_to_ble(const char *logLevel, const char *format, va_list arg)
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    if (config.security.debug_log_api_enabled && !pauseBluetoothLogging) {
        bool isBleConnected = false;
#ifdef ARCH_ESP32
        isBleConnected = nimbleBluetooth && nimbleBluetooth->isActive() && nimbleBluetooth->isConnected();
#elif defined(ARCH_NRF52)
        isBleConnected = nrf52Bluetooth != nullptr && nrf52Bluetooth->isConnected();
#endif
        if (isBleConnected) {
            char *message;
            size_t initialLen;
            size_t len;
            initialLen = strlen(format);
            message = new char[initialLen + 1];
            len = vsnprintf(message, initialLen + 1, format, arg);
            if (len > initialLen) {
                delete[] message;
                message = new char[len + 1];
                vsnprintf(message, len + 1, format, arg);
            }
            auto thread = concurrency::OSThread::currentThread;
            meshtastic_LogRecord logRecord = meshtastic_LogRecord_init_zero;
            logRecord.level = getLogLevel(logLevel);
            strcpy(logRecord.message, message);
            if (thread)
                strcpy(logRecord.source, thread->ThreadName.c_str());
            logRecord.time = getValidTime(RTCQuality::RTCQualityDevice, true);

            uint8_t *buffer = new uint8_t[meshtastic_LogRecord_size];
            size_t size = pb_encode_to_bytes(buffer, meshtastic_LogRecord_size, meshtastic_LogRecord_fields, &logRecord);
#ifdef ARCH_ESP32
            nimbleBluetooth->sendLog(buffer, size);
#elif defined(ARCH_NRF52)
            nrf52Bluetooth->sendLog(buffer, size);
#endif
            delete[] message;
            delete[] buffer;
        }
    }
#else
    (void)logLevel;
    (void)format;
    (void)arg;
#endif
}

meshtastic_LogRecord_Level RedirectablePrint::getLogLevel(const char *logLevel)
{
    meshtastic_LogRecord_Level ll = meshtastic_LogRecord_Level_UNSET; // default to unset
    switch (logLevel[0]) {
    case 'D':
        ll = meshtastic_LogRecord_Level_DEBUG;
        break;
    case 'I':
        ll = meshtastic_LogRecord_Level_INFO;
        break;
    case 'W':
        ll = meshtastic_LogRecord_Level_WARNING;
        break;
    case 'E':
        ll = meshtastic_LogRecord_Level_ERROR;
        break;
    case 'C':
        ll = meshtastic_LogRecord_Level_CRITICAL;
        break;
    }
    return ll;
}

void RedirectablePrint::log(const char *logLevel, const char *format, ...)
{

    // append \n to format
    size_t len = strlen(format);
    char *newFormat = new char[len + 2];
    strcpy(newFormat, format);
    newFormat[len] = '\n';
    newFormat[len + 1] = '\0';

#if ARCH_PORTDUINO
    // level trace is special, two possible ways to handle it.
    if (strcmp(logLevel, MESHTASTIC_LOG_LEVEL_TRACE) == 0) {
        if (settingsStrings[traceFilename] != "") {
            va_list arg;
            va_start(arg, format);
            try {
                traceFile << va_arg(arg, char *) << std::endl;
            } catch (const std::ios_base::failure &e) {
            }
            va_end(arg);
        }
        if (settingsMap[logoutputlevel] < level_trace && strcmp(logLevel, MESHTASTIC_LOG_LEVEL_TRACE) == 0) {
            delete[] newFormat;
            return;
        }
    }
    if (settingsMap[logoutputlevel] < level_debug && strcmp(logLevel, MESHTASTIC_LOG_LEVEL_DEBUG) == 0) {
        delete[] newFormat;
        return;
    } else if (settingsMap[logoutputlevel] < level_info && strcmp(logLevel, MESHTASTIC_LOG_LEVEL_INFO) == 0) {
        delete[] newFormat;
        return;
    } else if (settingsMap[logoutputlevel] < level_warn && strcmp(logLevel, MESHTASTIC_LOG_LEVEL_WARN) == 0) {
        delete[] newFormat;
        return;
    }
#endif
    if (moduleConfig.serial.override_console_serial_port && strcmp(logLevel, MESHTASTIC_LOG_LEVEL_DEBUG) == 0) {
        delete[] newFormat;
        return;
    }

#ifdef HAS_FREE_RTOS
    if (inDebugPrint != nullptr && xSemaphoreTake(inDebugPrint, portMAX_DELAY) == pdTRUE) {
#else
    if (!inDebugPrint) {
        inDebugPrint = true;
#endif

        va_list arg;
        va_start(arg, format);

        log_to_serial(logLevel, newFormat, arg);
        log_to_syslog(logLevel, newFormat, arg);
        log_to_ble(logLevel, newFormat, arg);

        va_end(arg);
#ifdef HAS_FREE_RTOS
        xSemaphoreGive(inDebugPrint);
#else
        inDebugPrint = false;
#endif
    }

    delete[] newFormat;
    return;
}

void RedirectablePrint::hexDump(const char *logLevel, unsigned char *buf, uint16_t len)
{
    const char alphabet[17] = "0123456789abcdef";
    log(logLevel, "    +------------------------------------------------+ +----------------+");
    log(logLevel, "    |.0 .1 .2 .3 .4 .5 .6 .7 .8 .9 .a .b .c .d .e .f | |      ASCII     |");
    for (uint16_t i = 0; i < len; i += 16) {
        if (i % 128 == 0)
            log(logLevel, "    +------------------------------------------------+ +----------------+");
        char s[] = "|                                                | |                |\n";
        uint8_t ix = 1, iy = 52;
        for (uint8_t j = 0; j < 16; j++) {
            if (i + j < len) {
                uint8_t c = buf[i + j];
                s[ix++] = alphabet[(c >> 4) & 0x0F];
                s[ix++] = alphabet[c & 0x0F];
                ix++;
                if (c > 31 && c < 128)
                    s[iy++] = c;
                else
                    s[iy++] = '.';
            }
        }
        uint8_t index = i / 16;
        if (i < 256)
            log(logLevel, " ");
        log(logLevel, "%02x", index);
        log(logLevel, ".");
        log(logLevel, s);
    }
    log(logLevel, "    +------------------------------------------------+ +----------------+");
}

std::string RedirectablePrint::mt_sprintf(const std::string fmt_str, ...)
{
    int n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
    std::unique_ptr<char[]> formatted;
    va_list ap;
    while (1) {
        formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
        strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        int final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
    return std::string(formatted.get());
}
