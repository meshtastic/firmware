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
    if (q > currentQuality) {
        currentQuality = q;
        DEBUG_MSG("Setting RTC %ld secs\n", tv->tv_sec);
#ifndef NO_ESP32
        settimeofday(tv, NULL);
#else
        DEBUG_MSG("ERROR TIME SETTING NOT IMPLEMENTED!\n");
#endif
        readFromRTC();
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
    return ((millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t getValidTime()
{
    return (currentQuality >= RTCQualityFromNet) ? getTime() : 0;
}
