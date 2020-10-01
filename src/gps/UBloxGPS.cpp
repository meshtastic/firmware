#include "UBloxGPS.h"
#include "error.h"
#include <assert.h>

UBloxGPS::UBloxGPS() 
{
}

bool UBloxGPS::tryConnect()
{
    isConnected = false;

    if (_serial_gps)
        isConnected = ublox.begin(*_serial_gps);

    if (!isConnected && i2cAddress) {
        extern bool neo6M; // Super skanky - if we are talking to the device i2c we assume it is a neo7 on a RAK815, which
                           // supports the newer API
        neo6M = true;

        isConnected = ublox.begin(Wire, i2cAddress);
    }

    return isConnected;
}

bool UBloxGPS::setup()
{
    if (_serial_gps) {
#ifdef GPS_RX_PIN
        _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#else
        _serial_gps->begin(GPS_BAUDRATE);
#endif
        // _serial_gps.setRxBufferSize(1024); // the default is 256
    }

#ifdef GPS_POWER_EN
    pinMode(GPS_POWER_EN, OUTPUT);
    digitalWrite(GPS_POWER_EN, 1);
    delay(200); // Give time for the GPS to startup after we gave power
#endif

    // ublox.enableDebugging(Serial);

    // try a second time, the ublox lib serial parsing is buggy?
    // see https://github.com/meshtastic/Meshtastic-device/issues/376
    for (int i = 0; (i < 3) && !tryConnect(); i++)
        delay(500);

    if (isConnected) {
        DEBUG_MSG("Connected to UBLOX GPS successfully\n");

        GPS::setup();

        if (!setUBXMode())
            recordCriticalError(UBloxInitFailed); // Don't halt the boot if saving the config fails, but do report the bug

        return true;
    } else {
        // Note: we do not call superclass setup in this case, because we dont want sleep observer registered

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

    // ok = ublox.setAutoPVT(false); // Not implemented on NEO-6M
    // assert(ok);
    // ok = ublox.setDynamicModel(DYN_MODEL_BIKE); // probably PEDESTRIAN but just in case assume bike speeds
    // assert(ok);

    // per https://github.com/meshtastic/Meshtastic-device/issues/376 powerSaveMode might not work with the marginal
    // TTGO antennas
    // if (!ublox.powerSaveMode(true, 2000)) // use power save mode, the default timeout (1100ms seems a bit too tight)
    //     return false;

    if (!ublox.saveConfiguration(3000))
        return false;

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

    DEBUG_MSG("GPS Factory reset success=%d\n", isConnected);
    if (isConnected)
        ok = setUBXMode();

    return ok;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool UBloxGPS::lookForTime()
{
    if (ublox.getT(maxWait())) {
        /* Convert to unix time
        The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January
        1, 1970 (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
        */
        struct tm t;
        t.tm_sec = ublox.getSecond(0);
        t.tm_min = ublox.getMinute(0);
        t.tm_hour = ublox.getHour(0);
        t.tm_mday = ublox.getDay(0);
        t.tm_mon = ublox.getMonth(0) - 1;
        t.tm_year = ublox.getYear(0) - 1900;
        t.tm_isdst = false;
        perhapsSetRTC(t);
        return true;
    }
    else
    {
        return false;
    }
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

    // If we don't have a fix (a quick check), don't try waiting for a solution)
    uint8_t fixtype = ublox.getFixType(maxWait());
    DEBUG_MSG("GPS fix type %d\n", fixtype);

    // we only notify if position has changed due to a new fix
    if ((fixtype >= 3 && fixtype <= 4) && ublox.getP(maxWait())) // rd fixes only
    {
        latitude = ublox.getLatitude(0);
        longitude = ublox.getLongitude(0);
        altitude = ublox.getAltitudeMSL(0) / 1000; // in mm convert to meters
        dop = ublox.getPDOP(0); // PDOP (an accuracy metric) is reported in 10^2 units so we have to scale down when we use it
        heading = ublox.getHeading(0);
        numSatellites = ublox.getSIV(0);

        // bogus lat lon is reported as 0 or 0 (can be bogus just for one)
        // Also: apparently when the GPS is initially reporting lock it can output a bogus latitude > 90 deg!
        foundLocation =
            (latitude != 0) && (longitude != 0) && (latitude <= 900000000 && latitude >= -900000000) && (numSatellites > 0);
    } 

    return foundLocation;
}

bool UBloxGPS::whileIdle()
{
    // if using i2c or serial look too see if any chars are ready
    return ublox.checkUblox(); // See if new data is available. Process bytes as they come in. 
}


/// If possible force the GPS into sleep/low power mode
/// Note: ublox doesn't need a wake method, because as soon as we send chars to the GPS it will wake up
void UBloxGPS::sleep() {
    if (isConnected)
        ublox.powerOff();
}

