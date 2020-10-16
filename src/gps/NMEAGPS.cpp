#include "NMEAGPS.h"
#include "RTC.h"
#include "configuration.h"

static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

bool NMEAGPS::setupGPS()
{
#ifdef PIN_GPS_PPS
    // pulse per second
    // FIXME - move into shared GPS code
    pinMode(PIN_GPS_PPS, INPUT);
#endif

    return true;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool NMEAGPS::lookForTime()
{
    auto ti = reader.time;
    auto d = reader.date;
    if (ti.isValid() && d.isValid()) { // Note: we don't check for updated, because we'll only be called if needed
        /* Convert to unix time
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
(midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
*/
        struct tm t;
        t.tm_sec = ti.second();
        t.tm_min = ti.minute();
        t.tm_hour = ti.hour();
        t.tm_mday = d.day();
        t.tm_mon = d.month() - 1;
        t.tm_year = d.year() - 1900;
        t.tm_isdst = false;
        perhapsSetRTC(RTCQualityGPS, t);

        return true;
    } else
        return false;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool NMEAGPS::lookForLocation()
{
    bool foundLocation = false;

    // uint8_t fixtype = reader.fixQuality();
    // hasValidLocation = ((fixtype >= 1) && (fixtype <= 5));

    if (reader.satellites.isUpdated()) {
        setNumSatellites(reader.satellites.value());
    }

    // Diminution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
    if (reader.hdop.isUpdated()) {
        dop = reader.hdop.value();
    }
    if (reader.course.isUpdated()) {
        heading = reader.course.value() * 1e3; // Scale the heading (in degrees * 10^-2) to match the expected degrees * 10^-5
    }

    if (reader.altitude.isUpdated())
        altitude = reader.altitude.meters();

    if (reader.location.isUpdated()) {

        auto loc = reader.location.value();
        latitude = toDegInt(loc.lat);
        longitude = toDegInt(loc.lng);
        foundLocation = true;

        // expect gps pos lat=37.520825, lon=-122.309162, alt=158
        DEBUG_MSG("new NMEA GPS pos lat=%f, lon=%f, alt=%d, hdop=%g, heading=%f\n", latitude * 1e-7, longitude * 1e-7, altitude,
                  dop * 1e-2, heading * 1e-5);
    }

    return foundLocation;
}

bool NMEAGPS::whileIdle()
{
    bool isValid = false;

    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        DEBUG_MSG("%c", c);
        isValid |= reader.encode(c);
    }

    return isValid;
}
