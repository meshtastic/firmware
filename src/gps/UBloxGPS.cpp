#include "configuration.h"
#include "UBloxGPS.h"
#include "RTC.h"
#include "error.h"
#include "sleep.h"
#include <assert.h>

// if gps_update_interval below this value, do not powercycle the GPS
#define UBLOX_POWEROFF_THRESHOLD 90

#define PDOP_INVALID 9999

extern RadioConfig radioConfig;

UBloxGPS::UBloxGPS() {}

bool UBloxGPS::tryConnect()
{
    bool c = false;

    if (_serial_gps)
        c = ublox.begin(*_serial_gps);

    if (!c && i2cAddress) {
        c = ublox.begin(Wire, i2cAddress);
    }

    if (c)
        setConnected();

    return c;
}

bool UBloxGPS::setupGPS()
{
    GPS::setupGPS();

    // uncomment to see debug info
    ublox.enableDebugging(Serial);

    // try a second time, the ublox lib serial parsing is buggy?
    // see https://github.com/meshtastic/Meshtastic-device/issues/376
    for (int i = 0; (i < 3) && !tryConnect(); i++)
        delay(500);

    if (isConnected()) {
        DEBUG_MSG("Connected to UBLOX GPS successfully\n");

        if (!setUBXMode())
            RECORD_CRITICALERROR(CriticalErrorCode_UBloxInitFailed); // Don't halt the boot if saving the config fails, but do report the bug

        return true;

    } else {
        return false;
    }
}

bool UBloxGPS::setUBXMode()
{
    if (_serial_gps) {
        if (!ublox.setUART1Output(COM_TYPE_UBX, 1000)) // Use native API
            return false;
    }
    if (i2cAddress) {
        if (!ublox.setI2COutput(COM_TYPE_UBX, 1000))
            return false;
    }

    if (!ublox.setNavigationFrequency(1, 1000)) // Produce 4x/sec to keep the amount of time we stall in getPVT low
        return false;

    if (!ublox.saveConfiguration(3000))
        return false;

    ublox.setAutoPVT(true); //Tell the GNSS to "send" each solution

    return true;
}

/**
 * Reset our GPS back to factory settings
 *
 * @return true for success
 */
bool UBloxGPS::factoryReset()
{
    bool ok = false;

    // It is useful to force back into factory defaults (9600baud, NMEA to test the behavior of boards that don't have
    // GPS_TX connected)
    ublox.factoryReset();
    delay(5000);
    tryConnect(); // sets isConnected

    // try a second time, the ublox lib serial parsing is buggy?
    for (int i = 0; (i < 3) && !tryConnect(); i++)
        delay(500);

    DEBUG_MSG("GPS Factory reset success=%d\n", isConnected());
    if (isConnected())
        ok = setUBXMode();

    return ok;
}

/** Idle processing while GPS is looking for lock */
void UBloxGPS::whileActive()
{
    //ublox.flushPVT();  // reset ALL freshness flags first
    // Check for new Position, Velocity, Time data. getPVT returns true if new data is available.
    //ublox.getPVT();
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool UBloxGPS::lookForTime()
{
    /* Convert to unix time
    The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January
    1, 1970 (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
    */
    if (ublox.getPVT()) {
        struct tm t;
        t.tm_sec = ublox.getSecond();
        t.tm_min = ublox.getMinute();
        t.tm_hour = ublox.getHour();
        t.tm_mday = ublox.getDay();
        t.tm_mon = ublox.getMonth() - 1;
        t.tm_year = ublox.getYear() - 1900;
        t.tm_isdst = false;
        perhapsSetRTC(RTCQualityGPS, t);
        return true;
    }
    return false;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool UBloxGPS::lookForLocation()
{
    bool foundLocation = false;

    if (! ublox.getPVT()) {
        return false ;
    }

    fixType = ublox.getFixType();
    if (fixType == 0) {
        return false ;
    }
#ifdef UBLOX_EXTRAVERBOSE
    DEBUG_MSG("FixType=%d\n", fixType);
#endif

    // check if GPS has an acceptable lock
    //if (! hasLock()) {
        //ublox.flushPVT();  // reset ALL freshness flags
        //return false;
    //}

    // read lat/lon/alt/dop data into temporary variables to avoid
    // overwriting global variables with potentially invalid data
    int32_t tmp_dop = ublox.getPDOP(0); // PDOP (an accuracy metric) is reported in 10^2 units so we have to scale down when we use it
    int32_t tmp_lat = ublox.getLatitude(0);
    int32_t tmp_lon = ublox.getLongitude(0);
    int32_t tmp_alt_msl = ublox.getAltitudeMSL(0);
    int32_t tmp_alt_hae = ublox.getAltitude(0);
    int32_t max_dop = PDOP_INVALID;
    if (radioConfig.preferences.gps_max_dop)
        max_dop = radioConfig.preferences.gps_max_dop * 100;  // scaling

    // Note: heading is only currently implmented in the ublox for the 8m chipset - therefore
    // don't read it here - it will generate an ignored getPVT command on the 6ms
    // heading = ublox.getHeading(0);

    // read positional timestamp
    struct tm t;
    t.tm_sec = ublox.getSecond();
    t.tm_min = ublox.getMinute();
    t.tm_hour = ublox.getHour();
    t.tm_mday = ublox.getDay();
    t.tm_mon = ublox.getMonth() - 1;
    t.tm_year = ublox.getYear() - 1900;
    t.tm_isdst = false;

    time_t tmp_ts = mktime(&t);

    // FIXME - can opportunistically attempt to set RTC from GPS timestamp?

    // bogus lat lon is reported as 0 or 0 (can be bogus just for one)
    // Also: apparently when the GPS is initially reporting lock it can output a bogus latitude > 90 deg!
    // FIXME - NULL ISLAND is a real location on Earth!
    foundLocation = (tmp_lat != 0) && (tmp_lon != 0) &&
                    (tmp_lat <= 900000000) && (tmp_lat >= -900000000) &&
                    (tmp_dop < max_dop);

    // only if entire dataset is valid, update globals from temp vars
    if (foundLocation) {
        p.location_source = Position_LocSource_LOCSRC_GPS_INTERNAL;
        p.longitude_i = tmp_lon;
        p.latitude_i = tmp_lat;
        if (fixType > 2) {
            // if fix is 2d, ignore altitude data
            p.altitude = tmp_alt_msl / 1000;
            p.altitude_hae = tmp_alt_hae / 1000;
            p.alt_geoid_sep = (tmp_alt_hae - tmp_alt_msl) / 1000;
        } else {
#ifdef GPS_EXTRAVERBOSE
            DEBUG_MSG("no altitude data (fixType=%d)\n", fixType);
#endif
            // clean up old values in case it's a 3d-2d fix transition
            p.altitude = p.altitude_hae = p.alt_geoid_sep = 0;
        }
        p.pos_timestamp = tmp_ts;
        p.PDOP = tmp_dop;
        p.fix_type = fixType;
        p.sats_in_view = ublox.getSIV(0);
        // In debug logs, identify position by @timestamp:stage (stage 1 = birth)
        DEBUG_MSG("lookForLocation() new pos@%x:1\n", tmp_ts);
    } else {
        // INVALID solution - should never happen
        DEBUG_MSG("Invalid location lat/lon/hae/dop %d/%d/%d/%d - discarded\n",
                tmp_lat, tmp_lon, tmp_alt_hae, tmp_dop);
    }

    ublox.flushPVT();  // reset ALL freshness flags at the end

    return foundLocation;
}

bool UBloxGPS::hasLock()
{
    if (radioConfig.preferences.gps_accept_2d)
        return (fixType >= 2 && fixType <= 4);
    else
        return (fixType >= 3 && fixType <= 4);
}

bool UBloxGPS::whileIdle()
{
    // if using i2c or serial look too see if any chars are ready
    return ublox.checkUblox(); // See if new data is available. Process bytes as they come in.
}

/// If possible force the GPS into sleep/low power mode
/// Note: ublox doesn't need a wake method, because as soon as we send chars to the GPS it will wake up
void UBloxGPS::sleep()
{
    if (radioConfig.preferences.gps_update_interval > UBLOX_POWEROFF_THRESHOLD) {
        // Tell GPS to power down until we send it characters on serial port (we leave vcc connected)
        // TODO ublox.powerOff(radioConfig.preferences.gps_update_interval * 1000);
        // setGPSPower(false);
    }
}

void UBloxGPS::wake()
{
    //if (radioConfig.preferences.gps_update_interval > UBLOX_POWEROFF_THRESHOLD) {
        //fixType = 0; // assume we have no fix yet
    //}

    // this is idempotent
    setGPSPower(true);

    // Note: no delay needed because now we leave gps power on always and instead use ublox.powerOff()
    // Give time for the GPS to boot
    // delay(200);
}
