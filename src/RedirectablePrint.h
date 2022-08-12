#pragma once

#include <Print.h>
#include <stdarg.h>

/**
 * A Printable that can be switched to squirt its bytes to a different sink.
 * This class is mostly useful to allow debug printing to be redirected away from Serial
 * to some other transport if we switch Serial usage (on the fly) to some other purpose.
 */
class RedirectablePrint : public Print
{
    Print *dest;

    /// Used to allow multiple logDebug messages to appear on a single log line
    bool isContinuationMessage = false;

    volatile bool inDebugPrint = false;

  public:
    explicit RedirectablePrint(Print *_dest) : dest(_dest) {}

    /**
     * Set a new destination
     */
    void setDestination(Print *dest);

    virtual size_t write(uint8_t c);

    /**
     * Debug logging print message
     * 
     * If the provide format string ends with a newline we assume it is the final print of a single
     * log message.  Otherwise we assume more prints will come before the log message ends.  This
     * allows you to call logDebug a few times to build up a single log message line if you wish.
     * 
     * FIXME, eventually add log levels (INFO, WARN, ERROR) and subsystems.  Move into 
     * a different class.
     */
    size_t logDebug(const char * format, ...)  __attribute__ ((format (printf, 2, 3)));

    /** like printf but va_list based */
    size_t vprintf(const char *format, va_list arg);
};

class NoopPrint : public Print
{
  public:
    virtual size_t write(uint8_t c) { return 1; }
};

/**
 * A printer that doesn't go anywhere
 */
extern NoopPrint noopPrint;
