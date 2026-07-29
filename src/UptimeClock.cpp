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

uint64_t Time::getMillis64()
{
    static uint32_t lastLow = 0;  // last 32-bit sample
    static uint32_t highWord = 0; // number of observed wraps

    uint32_t now = Time::getMillis();
#ifdef PIO_UNIT_TESTING
    // A test swapping clock sources (real <-> injected) can make `now` jump backward for
    // reasons other than a genuine wrap - rebase rather than miscount it as one.
    if (Time::clockSourceChanged) {
        lastLow = now;
        highWord = 0;
        Time::clockSourceChanged = false;
    }
#endif
    if (now < lastLow)
        highWord++; // low word wrapped since last call
    lastLow = now;
    return (static_cast<uint64_t>(highWord) << 32) | now;
}
