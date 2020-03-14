
#include "GPS.h"
#include "time.h"
#include <sys/time.h>
#include "configuration.h"

HardwareSerial _serial_gps(GPS_SERIAL_NUM);

RTC_DATA_ATTR bool timeSetFromGPS; // We only reset our time once per _boot_ after that point just run from the internal clock (even across sleeps)

GPS gps;

// stuff that really should be in in the instance instead...
static uint32_t timeStartMsec;  // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

static bool hasValidLocation; // default to false, until we complete our first read
static bool wantNewLocation = true;

GPS::GPS() : PeriodicTask()
{
}

void GPS::setup()
{
    readFromRTC(); // read the main CPU RTC at first

#ifdef GPS_RX_PIN
    _serial_gps.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    // ublox.enableDebugging(Serial);

    // note: the lib's implementation has the wrong docs for what the return val is
    // it is not a bool, it returns zero for success
    isConnected = ublox.begin(_serial_gps);
    if (isConnected)
    {
        DEBUG_MSG("Connected to GPS successfully\n");

        bool ok = ublox.setUART1Output(COM_TYPE_UBX); // Use native API
        assert(ok);
        ok = ublox.setNavigationFrequency(1); //Produce one solutions per second
        assert(ok);
        ok = ublox.setAutoPVT(false);
        assert(ok);
        ok = ublox.setDynamicModel(DYN_MODEL_BIKE); // probably PEDESTRIAN but just in case assume bike speeds
        assert(ok);
        ok = ublox.saveConfiguration();
        assert(ok);
    }
    else
    {
        // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.  Just
        // assume NEMA at 9600 baud.
        DEBUG_MSG("ERROR: No bidirectional GPS found, hoping that it still might work\n");

        // tell lib, we are expecting the module to send PVT messages by itself to our Rx pin
        // you can set second parameter to "false" if you want to control the parsing and eviction of the data (need to call checkUblox cyclically)
        ublox.assumeAutoPVT(true, true);
    }
#endif
}

void GPS::readFromRTC()
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
void GPS::perhapsSetRTC(const struct timeval *tv)
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
void GPS::loop()
{
    PeriodicTask::loop();
}

uint32_t GPS::getTime()
{
    return ((millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t GPS::getValidTime()
{
    return timeSetFromGPS ? getTime() : 0;
}

/// Returns true if we think the board can enter deep or light sleep now (we might be trying to get a GPS lock)
bool GPS::canSleep()
{
    return true; // we leave GPS on during sleep now, so sleep is okay !wantNewLocation;
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
void GPS::prepareSleep()
{
    // discard all rx serial bytes so we don't try to parse them when we come back
}

void GPS::doTask()
{
#ifdef GPS_RX_PIN
    // Consume all characters that have arrived

    // getPVT automatically calls checkUblox
    // ublox.checkUblox(); //See if new data is available. Process bytes as they come in.
    DEBUG_MSG("numsat %d, sec %d\n", ublox.getSIV(), ublox.getSecond());

#if 0
    if (ublox.getPVT())
    { // we only notify if position has changed
        isConnected = true; // We just received a packet, so we must have a GPS

        if (!timeSetFromGPS)
        {
            struct timeval tv;

            /* Convert to unix time 
        The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970 (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z). 
        */
            struct tm t;
            t.tm_sec = ublox.getSecond();
            t.tm_min = ublox.getMinute();
            t.tm_hour = ublox.getHour();
            t.tm_mday = ublox.getDay();
            t.tm_mon = ublox.getMonth() - 1;
            t.tm_year = ublox.getYear() - 1900;
            t.tm_isdst = false;
            time_t res = mktime(&t);
            tv.tv_sec = res;
            tv.tv_usec = 0; // time.centisecond() * (10 / 1000);

            DEBUG_MSG("Got time from GPS month=%d, year=%d, unixtime=%ld\n", ublox.getMonth(), ublox.getYear(), tv.tv_sec);

            // FIXME
            // perhapsSetRTC(&tv);
        }

        DEBUG_MSG("new gps pos\n");
        latitude = ublox.getLatitude() * 10e-7;
        longitude = ublox.getLongitude() * 10e-7;
        altitude = ublox.getAltitude() / 1000; // in mm convert to meters
        hasValidLocation = true;
        wantNewLocation = false;
        notifyObservers();
    }
    else // we didn't get a location update, go back to sleep and hope the characters show up
        wantNewLocation = true;
#endif
#endif

    // Once we have sent a location once we only poll the GPS rarely, otherwise check back every 100ms until we have something over the serial
    setPeriod(hasValidLocation && !wantNewLocation ? 30 * 1000 : 100);
}

void GPS::startLock()
{
    DEBUG_MSG("Looking for GPS lock\n");
    wantNewLocation = true;
    setPeriod(1);
}
