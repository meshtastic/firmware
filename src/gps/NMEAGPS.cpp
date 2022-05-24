#include "configuration.h"
#include "NMEAGPS.h"
#include "RTC.h"

#include <TinyGPS++.h>

// GPS solutions older than this will be rejected - see TinyGPSDatum::age()
#define GPS_SOL_EXPIRY_MS 5000   // in millis. give 1 second time to combine different sentences. NMEA Frequency isn't higher anyway
#define NMEA_MSG_GXGSA "GNGSA"  // GSA message (GPGSA, GNGSA etc)

static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

bool NMEAGPS::factoryReset()
{
#ifdef GPS_UBLOX
    // Factory Reset
    byte _message_reset[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF,
        0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0x00, 0x00, 0x17, 0x2B, 0x7E};
    _serial_gps->write(_message_reset,sizeof(_message_reset));
    delay(1000);
#endif
    return true;
}

bool NMEAGPS::setupGPS()
{
    GPS::setupGPS();
    
#ifdef PIN_GPS_PPS
    // pulse per second
    // FIXME - move into shared GPS code
    pinMode(PIN_GPS_PPS, INPUT);
#endif

// Currently disabled per issue #525 (TinyGPS++ crash bug)
// when fixed upstream, can be un-disabled to enable 3D FixType and PDOP
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // see NMEAGPS.h
    gsafixtype.begin(reader, NMEA_MSG_GXGSA, 2);
    gsapdop.begin(reader, NMEA_MSG_GXGSA, 15);
    DEBUG_MSG("Using " NMEA_MSG_GXGSA " for 3DFIX and PDOP\n");
#else
    DEBUG_MSG("GxGSA NOT available\n");
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
        if (t.tm_mon > -1){
            DEBUG_MSG("NMEA GPS time %02d-%02d-%02d %02d:%02d:%02d\n", d.year(), d.month(), t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
            perhapsSetRTC(RTCQualityGPS, t);
            return true;
        } else
            return false;
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
    // By default, TinyGPS++ does not parse GPGSA lines, which give us 
    //   the 2D/3D fixType (see NMEAGPS.h)
    // At a minimum, use the fixQuality indicator in GPGGA (FIXME?)
    fixQual = reader.fixQuality();

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    fixType = atoi(gsafixtype.value());  // will set to zero if no data
    DEBUG_MSG("FIX QUAL=%d, TYPE=%d\n", fixQual, fixType);
#endif

    // check if GPS has an acceptable lock
    if (! hasLock())
        return false;

#ifdef GPS_EXTRAVERBOSE
    DEBUG_MSG("AGE: LOC=%d FIX=%d DATE=%d TIME=%d\n", 
                reader.location.age(), 
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
                gsafixtype.age(),
#else
                0,
#endif
                reader.date.age(), reader.time.age());
#endif  // GPS_EXTRAVERBOSE

    // check if a complete GPS solution set is available for reading
    //   tinyGPSDatum::age() also includes isValid() test
    // FIXME
    if (! ((reader.location.age() < GPS_SOL_EXPIRY_MS) &&
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
            (gsafixtype.age() < GPS_SOL_EXPIRY_MS) &&
#endif
            (reader.time.age() < GPS_SOL_EXPIRY_MS) &&
            (reader.date.age() < GPS_SOL_EXPIRY_MS)))
    {
        DEBUG_MSG("SOME data is TOO OLD: LOC %u, TIME %u, DATE %u\n", reader.location.age(), reader.time.age(), reader.date.age());
        return false;
    }

    // Is this a new point or are we re-reading the previous one?
    if (! reader.location.isUpdated())
        return false;

    // We know the solution is fresh and valid, so just read the data
    auto loc = reader.location.value();

    // Bail out EARLY to avoid overwriting previous good data (like #857)
    if (toDegInt(loc.lat) > 900000000) {
#ifdef GPS_EXTRAVERBOSE        
        DEBUG_MSG("Bail out EARLY on LAT %i\n",toDegInt(loc.lat));
#endif
        return false;
    }
    if (toDegInt(loc.lng) > 1800000000) {
#ifdef GPS_EXTRAVERBOSE        
        DEBUG_MSG("Bail out EARLY on LNG %i\n",toDegInt(loc.lng));
#endif
        return false;
    }

    p.location_source = Position_LocSource_LOCSRC_GPS_INTERNAL;

    // Dilution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.HDOP = reader.hdop.value();
    p.PDOP = TinyGPSPlus::parseDecimal(gsapdop.value());
    DEBUG_MSG("PDOP=%d, HDOP=%d\n", dop, reader.hdop.value());
#else
    // FIXME! naive PDOP emulation (assumes VDOP==HDOP)
    // correct formula is PDOP = SQRT(HDOP^2 + VDOP^2)
    p.HDOP = reader.hdop.value();
    p.PDOP = 1.41 * reader.hdop.value();
#endif

    // Discard incomplete or erroneous readings
    if (reader.hdop.value() == 0)
        return false;

    p.latitude_i = toDegInt(loc.lat);
    p.longitude_i = toDegInt(loc.lng);

    p.alt_geoid_sep = reader.geoidHeight.meters();
    p.altitude_hae = reader.altitude.meters() + p.alt_geoid_sep;
    p.altitude = reader.altitude.meters();

    p.fix_quality = fixQual;
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.fix_type = fixType;
#endif

    // positional timestamp
    struct tm t;
    t.tm_sec = reader.time.second();
    t.tm_min = reader.time.minute();
    t.tm_hour = reader.time.hour();
    t.tm_mday = reader.date.day();
    t.tm_mon = reader.date.month() - 1;
    t.tm_year = reader.date.year() - 1900;
    t.tm_isdst = false;
    p.pos_timestamp = mktime(&t);

    // Nice to have, if available
    if (reader.satellites.isUpdated()) {
        p.sats_in_view = reader.satellites.value();
    }

    if (reader.course.isUpdated() && reader.course.isValid()) {
        if (reader.course.value() < 36000) {  // sanity check
            p.ground_track = reader.course.value() * 1e3; // Scale the heading (in degrees * 10^-2) to match the expected degrees * 10^-5
        } else {
            DEBUG_MSG("BOGUS course.value() REJECTED: %d\n",
                        reader.course.value());
        }
    }

/*
    // REDUNDANT?
    // expect gps pos lat=37.520825, lon=-122.309162, alt=158
    DEBUG_MSG("new NMEA GPS pos lat=%f, lon=%f, alt=%d, dop=%g, heading=%f\n",
              latitude * 1e-7, longitude * 1e-7, altitude, dop * 1e-2,
              heading * 1e-5);
*/
    return true;
}


bool NMEAGPS::hasLock()
{
    // Using GPGGA fix quality indicator
    if (fixQual >= 1 && fixQual <= 5) {
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
        // Use GPGSA fix type 2D/3D (better) if available
        if (fixType == 3 || fixType == 0)  // zero means "no data received"
#endif
            return true;
    }

    return false;
}

bool NMEAGPS::hasFlow()
{
    return reader.passedChecksum() > 0;
}

bool NMEAGPS::whileIdle()
{
    bool isValid = false;

    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        // DEBUG_MSG("%c", c);
        isValid |= reader.encode(c);
    }

    return isValid;
}
