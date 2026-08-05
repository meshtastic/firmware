/**
 * @file Time.cpp
 * @brief Monotonic uptime clock with test injection and a rollover-immune 64-bit form.
 *
 * getMillis() is a thin wrapper over the platform millis() (or the injected test clock).
 * getMillis64() extends it to 64 bits with a software carry: it samples the 32-bit clock and
 * bumps a high word each time the low word wraps. The sampler must be polled at least once per
 * ~49.7-day wrap window to catch every wrap — trivially satisfied by normal device activity.
 * It keeps mutable static state, so it is not ISR-safe (documented in Time.h).
 */
#include "Time.h"

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
    if (now < lastLow)
        highWord++; // low word wrapped since last call
    lastLow = now;
    return (static_cast<uint64_t>(highWord) << 32) | now;
}
