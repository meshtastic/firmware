
#include "GPSNema.h"
#include "time.h"
#include <sys/time.h>
#include "configuration.h"
#include "GPS.h"

// GPSNema gps;

// stuff that really should be in in the instance instead...
static uint32_t timeStartMsec;  // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

static bool hasValidLocation; // default to false, until we complete our first read
static bool wantNewLocation = true;

GPSNema::GPSNema() : PeriodicTask()
{
}

void GPSNema::setup()
{
    readFromRTC();

#ifdef GPS_RX_PIN
    _serial_gps.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#endif
}

void GPSNema::readFromRTC()
{
    struct timeval tv; /* btw settimeofday() is helpfull here too*/

    if (!gettimeofday(&tv, NULL))
    {
        uint32_t now = millis();

        DEBUG_MSG("Read RTC time as %ld (cur millis %u) valid=%d\n", tv.tv_sec, now, timeSetFromGPS);
        timeStartMsec = now;
        zeroOffsetSecs = tv.tv_sec;
    }
}

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
void GPSNema::perhapsSetRTC(const struct timeval *tv)
{
    if (!timeSetFromGPS)
    {
        timeSetFromGPS = true;
        DEBUG_MSG("Setting RTC %ld secs\n", tv->tv_sec);
        settimeofday(tv, NULL);
        readFromRTC();
    }
}

#include <time.h>

// for the time being we need to rapidly read from the serial port to prevent overruns
void GPSNema::loop()
{
    PeriodicTask::loop();
}

uint32_t GPSNema::getTime()
{
    return ((millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t GPSNema::getValidTime()
{
    return timeSetFromGPS ? getTime() : 0;
}

/// Returns true if we think the board can enter deep or light sleep now (we might be trying to get a GPS lock)
bool GPSNema::canSleep()
{
    return !wantNewLocation;
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
void GPSNema::prepareSleep()
{
    // discard all rx serial bytes so we don't try to parse them when we come back
    while (_serial_gps.available())
    {
        _serial_gps.read();
    }

    // make the parser bail on whatever it was parsing
    encode('\n');
}

void GPSNema::doTask()
{
#ifdef GPS_RX_PIN
    // Consume all characters that have arrived

    while (_serial_gps.available())
    {
        encode(_serial_gps.read());
        // DEBUG_MSG("Got GPS response\n");
    }

    if (!timeSetFromGPS && time.isValid() && date.isValid())
    {
        struct timeval tv;

        DEBUG_MSG("Got time from GPS\n");

        /* Convert to unix time 
        The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970 (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z). 
        */
        struct tm t;
        t.tm_sec = time.second();
        t.tm_min = time.minute();
        t.tm_hour = time.hour();
        t.tm_mday = date.day();
        t.tm_mon = date.month() - 1;
        t.tm_year = date.year() - 1900;
        t.tm_isdst = false;
        time_t res = mktime(&t);
        tv.tv_sec = res;
        tv.tv_usec = 0; // time.centisecond() * (10 / 1000);

        perhapsSetRTC(&tv);
    }
#endif

    if (location.isValid() && location.isUpdated())
    { // we only notify if position has changed
        // DEBUG_MSG("new gps pos\n");
        hasValidLocation = true;
        wantNewLocation = false;
        notifyObservers();
    }
    else // we didn't get a location update, go back to sleep and hope the characters show up
        wantNewLocation = true;

    // Once we have sent a location once we only poll the GPS rarely, otherwise check back every 100ms until we have something over the serial
    setPeriod(hasValidLocation && !wantNewLocation ? 30 * 1000 : 100);
}

void GPSNema::startLock()
{
    DEBUG_MSG("Looking for GPS lock\n");
    wantNewLocation = true;
    setPeriod(1);
}

String GPSNema::getTimeStr()
{
    static char t[12]; // used to sprintf for Serial output

    snprintf(t, sizeof(t), "%02d:%02d:%02d", time.hour(), time.minute(), time.second());
    return t;
}
