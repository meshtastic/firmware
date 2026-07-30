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

namespace
{
// Carry state for getMillisMonotonic(). Written on every read; not ISR-safe.
uint32_t lastLowMs;
uint32_t wrapsSinceBoot;
} // namespace

uint64_t Time::getMillisMonotonic()
{
    uint32_t now = getMillis();
    if (now < lastLowMs)
        wrapsSinceBoot++;
    lastLowMs = now;
    return ((uint64_t)wrapsSinceBoot << 32) | now;
}

uint32_t Time::getUptimeSecs()
{
    return (uint32_t)(getMillisMonotonic() / 1000);
}

#ifdef PIO_UNIT_TESTING
void Time::resetMonotonicForTests()
{
    lastLowMs = 0;
    wrapsSinceBoot = 0;
}
#endif
