
#include "GPS.h"
#include "configuration.h"
#include <assert.h>
#include <time.h>

// If we have a serial GPS port it will not be null
#ifdef GPS_RX_PIN
HardwareSerial _serial_gps_real(GPS_SERIAL_NUM);
HardwareSerial *GPS::_serial_gps = &_serial_gps_real;
#elif defined(NRF52840_XXAA)
// Assume NRF52840
HardwareSerial *GPS::_serial_gps = &Serial1;
#else
HardwareSerial *GPS::_serial_gps = NULL;
#endif

#ifdef GPS_I2C_ADDRESS
uint8_t GPS::i2cAddress = GPS_I2C_ADDRESS;
#else
uint8_t GPS::i2cAddress = 0;
#endif

bool timeSetFromGPS; // We try to set our time from GPS each time we wake from sleep

GPS *gps;

// stuff that really should be in in the instance instead...
static uint32_t
    timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

void readFromRTC()
{
    struct timeval tv; /* btw settimeofday() is helpfull here too*/

    if (!gettimeofday(&tv, NULL)) {
        uint32_t now = millis();

        DEBUG_MSG("Read RTC time as %ld (cur millis %u) valid=%d\n", tv.tv_sec, now, timeSetFromGPS);
        timeStartMsec = now;
        zeroOffsetSecs = tv.tv_sec;
    }
}

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
void perhapsSetRTC(const struct timeval *tv)
{
    if (!timeSetFromGPS) {
        timeSetFromGPS = true;
        DEBUG_MSG("Setting RTC %ld secs\n", tv->tv_sec);
#ifndef NO_ESP32
        settimeofday(tv, NULL);
#else
        DEBUG_MSG("ERROR TIME SETTING NOT IMPLEMENTED!\n");
#endif
        readFromRTC();
    }
}

void perhapsSetRTC(struct tm &t)
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
    if (t.tm_year < 0 || t.tm_year >= 300)
        DEBUG_MSG("Ignoring invalid GPS month=%d, year=%d, unixtime=%ld\n", t.tm_mon, t.tm_year, tv.tv_sec);
    else
        perhapsSetRTC(&tv);
}

uint32_t getTime()
{
    return ((millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t getValidTime()
{
    return timeSetFromGPS ? getTime() : 0;
}
