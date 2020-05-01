
#include "GPS.h"
#include "configuration.h"
#include "time.h"
#include <assert.h>
#include <sys/time.h>

#ifdef GPS_RX_PIN
HardwareSerial _serial_gps(GPS_SERIAL_NUM);
#else
// Assume NRF52
HardwareSerial &_serial_gps = Serial1;
#endif

bool timeSetFromGPS; // We try to set our time from GPS each time we wake from sleep

GPS gps;

// stuff that really should be in in the instance instead...
static uint32_t
    timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

static bool wantNewLocation = true;

GPS::GPS() : PeriodicTask() {}

void GPS::setup()
{
    PeriodicTask::setup();

    readFromRTC(); // read the main CPU RTC at first

#ifdef GPS_RX_PIN
    _serial_gps.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#else
    _serial_gps.begin(GPS_BAUDRATE);
#endif
    // _serial_gps.setRxBufferSize(1024); // the default is 256
    // ublox.enableDebugging(Serial);

    // note: the lib's implementation has the wrong docs for what the return val is
    // it is not a bool, it returns zero for success
    isConnected = ublox.begin(_serial_gps);

    // try a second time, the ublox lib serial parsing is buggy?
    if (!isConnected)
        isConnected = ublox.begin(_serial_gps);

    if (isConnected) {
        DEBUG_MSG("Connected to GPS successfully\n");

        bool factoryReset = false;
        bool ok;
        if (factoryReset) {
            // It is useful to force back into factory defaults (9600baud, NEMA to test the behavior of boards that don't have
            // GPS_TX connected)
            ublox.factoryReset();
            delay(2000);
            isConnected = ublox.begin(_serial_gps);
            DEBUG_MSG("Factory reset success=%d\n", isConnected);
            if (isConnected) {
                ublox.assumeAutoPVT(true, true); // Just parse NEMA for now
            }
        } else {
            ok = ublox.setUART1Output(COM_TYPE_UBX, 500); // Use native API
            assert(ok);
            ok = ublox.setNavigationFrequency(1, 500); // Produce 4x/sec to keep the amount of time we stall in getPVT low
            assert(ok);
            // ok = ublox.setAutoPVT(false); // Not implemented on NEO-6M
            // assert(ok);
            // ok = ublox.setDynamicModel(DYN_MODEL_BIKE); // probably PEDESTRIAN but just in case assume bike speeds
            // assert(ok);
            ok = ublox.powerSaveMode(); // use power save mode
            assert(ok);
        }
        ok = ublox.saveConfiguration(3000);
        assert(ok);
    } else {
        // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.  Just
        // assume NEMA at 9600 baud.
        DEBUG_MSG("ERROR: No bidirectional GPS found, hoping that it still might work\n");

        // tell lib, we are expecting the module to send PVT messages by itself to our Rx pin
        // you can set second parameter to "false" if you want to control the parsing and eviction of the data (need to call
        // checkUblox cyclically)
        ublox.assumeAutoPVT(true, true);
    }
}

void GPS::readFromRTC()
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
void GPS::perhapsSetRTC(const struct timeval *tv)
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

#include <time.h>

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
    if (isConnected)
        ublox.powerOff();
}

void GPS::doTask()
{
    uint8_t fixtype = 3; // If we are only using the RX pin, assume we have a 3d fix

    if (isConnected) {
        // Consume all characters that have arrived

        // getPVT automatically calls checkUblox
        ublox.checkUblox(); // See if new data is available. Process bytes as they come in.

        // If we don't have a fix (a quick check), don't try waiting for a solution)
        // Hmmm my fix type reading returns zeros for fix, which doesn't seem correct, because it is still sptting out positions
        // turn off for now
        // fixtype = ublox.getFixType();
        DEBUG_MSG("fix type %d\n", fixtype);
    }

    // DEBUG_MSG("sec %d\n", ublox.getSecond());
    // DEBUG_MSG("lat %d\n", ublox.getLatitude());

    // any fix that has time
    if (!timeSetFromGPS && ublox.getT()) {
        struct timeval tv;

        /* Convert to unix time
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
(midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
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

        DEBUG_MSG("Got time from GPS month=%d, year=%d, unixtime=%ld\n", t.tm_mon, t.tm_year, tv.tv_sec);
        if (t.tm_year < 0 || t.tm_year >= 300)
            DEBUG_MSG("Ignoring invalid GPS time\n");
        else
            perhapsSetRTC(&tv);
    }

    if ((fixtype >= 3 && fixtype <= 4) && ublox.getP()) // rd fixes only
    {
        // we only notify if position has changed
        latitude = ublox.getLatitude() * 1e-7;
        longitude = ublox.getLongitude() * 1e-7;
        altitude = ublox.getAltitude() / 1000; // in mm convert to meters
        DEBUG_MSG("new gps pos lat=%f, lon=%f, alt=%d\n", latitude, longitude, altitude);

        hasValidLocation = (latitude != 0) || (longitude != 0); // bogus lat lon is reported as 0,0
        if (hasValidLocation) {
            wantNewLocation = false;
            notifyObservers(NULL);
            // ublox.powerOff();
        }
    } else // we didn't get a location update, go back to sleep and hope the characters show up
        wantNewLocation = true;

    // Once we have sent a location once we only poll the GPS rarely, otherwise check back every 1s until we have something over
    // the serial
    setPeriod(hasValidLocation && !wantNewLocation ? 30 * 1000 : 10 * 1000);
}

void GPS::startLock()
{
    DEBUG_MSG("Looking for GPS lock\n");
    wantNewLocation = true;
    setPeriod(1);
}
