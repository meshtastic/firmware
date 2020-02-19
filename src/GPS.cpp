
#include "GPS.h"
#include "time.h"
#include <sys/time.h>

// stuff that really should be in in the instance instead...
HardwareSerial _serial_gps(GPS_SERIAL_NUM);
uint32_t timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
uint64_t zeroOffset;    // GPS based time in millis since 1970 - only updated once on initial lock
bool timeSetFromGPS;    // We only reset our time once per wake

GPS gps;

GPS::GPS() : PeriodicTask(30 * 1000)
{
}

void GPS::setup()
{
    readFromRTC();

#ifdef GPS_RX_PIN
    _serial_gps.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#endif
}

void GPS::readFromRTC()
{
    struct timeval tv; /* btw settimeofday() is helpfull here too*/

    if (!gettimeofday(&tv, NULL))
    {
        uint32_t now = millis();

        DEBUG_MSG("Read RTC time as %ld (cur millis %u)\n", tv.tv_sec, now);
        timeStartMsec = now;
        zeroOffset = tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
    }
}

// for the time being we need to rapidly read from the serial port to prevent overruns
void GPS::loop()
{
    PeriodicTask::loop();

#ifdef GPS_RX_PIN
    while (_serial_gps.available())
    {
        encode(_serial_gps.read());
    }

    if (!timeSetFromGPS && time.isValid() && date.isValid())
    {
        timeSetFromGPS = true;

        struct timeval tv;

        // FIXME, this is a shit not right version of the standard def of unix time!!!
        tv.tv_sec = time.second() + time.minute() * 60 + time.hour() * 60 * 60 +
                    24 * 60 * 60 * (date.month() * 31 + date.day() + 365 * (date.year() - 1970));

        tv.tv_usec = time.centisecond() * (10 / 1000);

        DEBUG_MSG("Setting RTC %ld secs\n", tv.tv_sec);
        settimeofday(&tv, NULL);
        readFromRTC();
    }
#endif
}

uint64_t GPS::getTime()
{
    return ((uint64_t)millis() - timeStartMsec) + zeroOffset;
}

void GPS::doTask()
{
    if (location.isValid() && location.isUpdated())
    { // we only notify if position has changed
        // DEBUG_MSG("new gps pos\n");
        notifyObservers();
    }
}

String GPS::getTimeStr()
{
    static char t[12]; // used to sprintf for Serial output

    snprintf(t, sizeof(t), "%02d:%02d:%02d", time.hour(), time.minute(), time.second());
    return t;
}
