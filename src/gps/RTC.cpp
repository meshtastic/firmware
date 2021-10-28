#include "RTC.h"
#include "configuration.h"
#include <sys/time.h>
#include <time.h>

static RTCQuality currentQuality = RTCQualityNone;

RTCQuality getRTCQuality()
{
    return currentQuality;
}

// stuff that really should be in in the instance instead...
static uint32_t
    timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

void readFromRTC()
{
    struct timeval tv; /* btw settimeofday() is helpfull here too*/

    if (!gettimeofday(&tv, NULL)) {
        uint32_t now = millis();

        DEBUG_MSG("Read RTC time as %ld (cur millis %u) quality=%d\n", tv.tv_sec, now, currentQuality);
        timeStartMsec = now;
        zeroOffsetSecs = tv.tv_sec;
    }
}

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
bool perhapsSetRTC(RTCQuality q, const struct timeval *tv)
{
    static uint32_t lastSetMsec = 0;
    uint32_t now = millis();

    bool shouldSet;
    if (q > currentQuality) {
        currentQuality = q;
        shouldSet = true;
        DEBUG_MSG("Upgrading time to RTC %ld secs (quality %d)\n", tv->tv_sec, q);
    } else if(q == RTCQualityGPS && (now - lastSetMsec) > (12 * 60 * 60 * 1000UL)) {
        // Every 12 hrs we will slam in a new GPS time, to correct for local RTC clock drift
        shouldSet = true;
        DEBUG_MSG("Reapplying external time to correct clock drift %ld secs\n", tv->tv_sec);
    }
    else
        shouldSet = false;

    if (shouldSet) {
        lastSetMsec = now;

        // This delta value works on all platforms
        timeStartMsec = now;
        zeroOffsetSecs = tv->tv_sec;

        // If this platform has a setable RTC, set it
#ifndef NO_ESP32
        settimeofday(tv, NULL);
#endif

        // nrf52 doesn't have a readable RTC (yet - software not written)
#if defined(PORTDUINO) || !defined(NO_ESP32)
        readFromRTC();
#endif

        return true;
    } else {
        return false;
    }
}

bool perhapsSetRTC(RTCQuality q, struct tm &t)
{
    /* Convert to unix time
    The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
    (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
    */
    time_t res = mktime(&t);
    struct timeval tv;
    tv.tv_sec = res;
    tv.tv_usec = 0; // time.centisecond() * (10 / 1000);

    // DEBUG_MSG("Got time from GPS month=%d, year=%d, unixtime=%ld\n", t.tm_mon, t.tm_year, tv.tv_sec);
    if (t.tm_year < 0 || t.tm_year >= 300) {
        // DEBUG_MSG("Ignoring invalid GPS month=%d, year=%d, unixtime=%ld\n", t.tm_mon, t.tm_year, tv.tv_sec);
        return false;
    } else {
        return perhapsSetRTC(q, &tv);
    }
}

uint32_t getTime()
{
    return (((uint32_t) millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t getValidTime(RTCQuality minQuality)
{
    return (currentQuality >= minQuality) ? getTime() : 0;
}
