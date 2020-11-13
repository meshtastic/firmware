#pragma once

#include <Print.h>

/**
 * A Printable that can be switched to squirt its bytes to a different sink.
 * This class is mostly useful to allow debug printing to be redirected away from Serial
 * to some other transport if we switch Serial usage (on the fly) to some other purpose.
 */
class RedirectablePrint : public Print
{
    Print *dest;

  public:
    RedirectablePrint(Print *_dest) : dest(_dest) {}

    /**
     * Set a new destination
     */
    void setDestination(Print *dest);

    virtual size_t write(uint8_t c);
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