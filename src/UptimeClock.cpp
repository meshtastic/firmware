// See UptimeClock.h for the full contract.
#include "UptimeClock.h"
#include <Arduino.h>

uint32_t Time::getMillis()
{
#ifdef PIO_UNIT_TESTING
    if (Time::useTestClock)
        return Time::testNowMs;
#endif
    return millis();
}
