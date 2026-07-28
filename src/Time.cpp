// See Time.h for the full contract.
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
