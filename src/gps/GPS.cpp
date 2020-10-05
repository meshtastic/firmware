
#include "GPS.h"
#include "NodeDB.h"
#include "configuration.h"
#include "sleep.h"
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
bool perhapsSetRTC(const struct timeval *tv)
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
        return true;
    } else {
        return false;
    }
}

bool perhapsSetRTC(struct tm &t)
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
        return perhapsSetRTC(&tv);
    }
}

uint32_t getTime()
{
    return ((millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
}

uint32_t getValidTime()
{
    return timeSetFromGPS ? getTime() : 0;
}

bool GPS::setup()
{
    notifySleepObserver.observe(&notifySleep);

    return true;
}

/**
 * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
 *
 * calls sleep/wake
 */
void GPS::setAwake(bool on)
{
    if (!wakeAllowed && on) {
        DEBUG_MSG("Inhibiting because !wakeAllowed\n");
        on = false;
    }

    if (isAwake != on) {
        DEBUG_MSG("WANT GPS=%d\n", on);
        if (on) {
            lastWakeStartMsec = millis();
            wake();
        } else {
            lastSleepStartMsec = millis();
            sleep();
        }

        isAwake = on;
    }
}

/** Get how long we should stay looking for each aquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = radioConfig.preferences.gps_attempt_time;

    // fixme check modes
    if (t == 0)
        t = 30;

    t *= 1000; // msecs

    return t;
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = radioConfig.preferences.gps_update_interval;

    // fixme check modes
    if (t == 0)
        t = 30;

    t *= 1000;

    return t;
}

void GPS::publishUpdate()
{
    DEBUG_MSG("publishing GPS lock=%d\n", hasLock());

    // Notify any status instances that are observing us
    const meshtastic::GPSStatus status =
        meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop, heading, numSatellites);
    newStatus.notifyObservers(&status);
}

void GPS::loop()
{
    if (whileIdle()) {
        // if we have received valid NMEA claim we are connected
        isConnected = true;
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();
    bool mustPublishUpdate = false;

    if ((now - lastSleepStartMsec) > getSleepTime() && !isAwake) {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake) {
        // DEBUG_MSG("looking for location\n");
        whileActive();

        // If we've already set time from the GPS, no need to ask the GPS
        bool gotTime = timeSetFromGPS || lookForTime();
        bool gotLoc = lookForLocation();

        // We've been awake too long - force sleep
        bool tooLong = (now - lastWakeStartMsec) > getWakeTime();

        // Once we get a location we no longer desperately want an update
        if (gotLoc || tooLong) {
            if (gotLoc)
                hasValidLocation = true;

            if (tooLong) {
                // we didn't get a location during this ack window, therefore declare loss of lock
                hasValidLocation = false;
            }

            setAwake(false);
            publishUpdate(); // publish our update for this just finished acquisition window
        }
    }
}

void GPS::forceWake(bool on)
{
    if (on) {
        DEBUG_MSG("Looking for GPS lock\n");
        lastSleepStartMsec = 0; // Force an update ASAP
        wakeAllowed = true;
    } else {
        wakeAllowed = false;
        setAwake(false);
    }
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareSleep(void *unused)
{
    forceWake(false);

    return 0;
}
