#include "RedirectablePrint.h"
#include "RTC.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
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

    dest->write(c);
    return 1; // We always claim one was written, rather than trusting what the
              // serial port said (which could be zero)
}

size_t RedirectablePrint::vprintf(const char *format, va_list arg)
{
    va_list copy;

    va_copy(copy, arg);
    int len = vsnprintf(printBuf, printBufLen, format, copy);
    va_end(copy);
    if (len < 0) {
        va_end(arg);
        return 0;
    };
    if (len >= (int)printBufLen) {
        delete[] printBuf;
        printBufLen *= 2;
        printBuf = new char[printBufLen];
        len = vsnprintf(printBuf, printBufLen, format, arg);
    }

    len = Print::write(printBuf, len);
    return len;
}

size_t RedirectablePrint::logDebug(const char *format, ...)
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
            uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityFromNet);
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

                r += printf("%02d:%02d:%02d %u ", hour, min, sec, millis() / 1000);
            } else
                r += printf("??:??:?? %u ", millis() / 1000);

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