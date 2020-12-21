#include "RedirectablePrint.h"
#include "configuration.h"
#include <assert.h>

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
#ifdef SEGGER_STDOUT_CH
    SEGGER_RTT_PutCharSkip(SEGGER_STDOUT_CH, c);
#endif

    dest->write(c);
    return 1; // We always claim one was written, rather than trusting what the serial port said (which could be zero)
}