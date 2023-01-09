#include "configuration.h"
#include "RedirectablePrint.h"
#include "RTC.h"
#include "NodeDB.h"
#include "concurrency/OSThread.h"
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <cstring>

/**
 * A printer that doesn't go anywhere
 */
NoopPrint noopPrint;

void RedirectablePrint::setDestination(Print *_dest)
{
    assert(_dest);
    dest = _dest;
}

size_t RedirectablePrint::write(uint8_t c)
{
    // Always send the characters to our segger JTAG debugger
#ifdef SEGGER_STDOUT_CH
    SEGGER_RTT_PutChar(SEGGER_STDOUT_CH, c);
#endif

    if (!config.has_lora || config.device.serial_enabled)
        dest->write(c);

    return 1; // We always claim one was written, rather than trusting what the
              // serial port said (which could be zero)
}

size_t RedirectablePrint::vprintf(const char *format, va_list arg)
{
    va_list copy;
    static char printBuf[160];

    va_copy(copy, arg);
    size_t len = vsnprintf(printBuf, sizeof(printBuf), format, copy);
    va_end(copy);

    // If the resulting string is longer than sizeof(printBuf)-1 characters, the remaining characters are still counted for the return value

    if (len > sizeof(printBuf) - 1) {
        len = sizeof(printBuf) - 1;
        printBuf[sizeof(printBuf) - 2] = '\n';
    }

    len = Print::write(printBuf, len);
    return len;
}

size_t RedirectablePrint::log(const char *logLevel, const char *format, ...)
{
    size_t r = 0;

    if (!inDebugPrint) {
        inDebugPrint = true;

        va_list arg;
        va_start(arg, format);

        // Cope with 0 len format strings, but look for new line terminator
        bool hasNewline = *format && format[strlen(format) - 1] == '\n';

        // If we are the first message on a report, include the header
        if (!isContinuationMessage) {
            uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice);
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

                r += printf("%s | %02d:%02d:%02d %u ", logLevel, hour, min, sec, millis() / 1000);
            } else
                r += printf("%s | ??:??:?? %u ", logLevel, millis() / 1000);

            auto thread = concurrency::OSThread::currentThread;
            if (thread) {
                print("[");
                // printf("%p ", thread);
                // assert(thread->ThreadName.length());
                print(thread->ThreadName);
                print("] ");
            }
        }
        r += vprintf(format, arg);
        va_end(arg);

        isContinuationMessage = !hasNewline;
        inDebugPrint = false;
    }

    return r;
}

void RedirectablePrint::hexDump(const char *logLevel, unsigned char *buf, uint16_t len) {
  const char alphabet[17] = "0123456789abcdef";
  log(logLevel, "   +------------------------------------------------+ +----------------+\n");
  log(logLevel, "   |.0 .1 .2 .3 .4 .5 .6 .7 .8 .9 .a .b .c .d .e .f | |      ASCII     |\n");
  for (uint16_t i = 0; i < len; i += 16) {
    if (i % 128 == 0)
      log(logLevel, "   +------------------------------------------------+ +----------------+\n");
    char s[] = "|                                                | |                |\n";
    uint8_t ix = 1, iy = 52;
    for (uint8_t j = 0; j < 16; j++) {
      if (i + j < len) {
        uint8_t c = buf[i + j];
        s[ix++] = alphabet[(c >> 4) & 0x0F];
        s[ix++] = alphabet[c & 0x0F];
        ix++;
        if (c > 31 && c < 128) s[iy++] = c;
        else s[iy++] = '.';
      }
    }
    uint8_t index = i / 16;
    if (i < 256) log(logLevel, " ");
    log(logLevel, "%02x",index); 
    log(logLevel, ".");
    log(logLevel, s);
  }
  log(logLevel, "   +------------------------------------------------+ +----------------+\n");
}
