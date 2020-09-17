#include "UBloxGPS.h"
#include "error.h"
#include "sleep.h"
#include <assert.h>

UBloxGPS::UBloxGPS() : concurrency::PeriodicTask()
{
    notifySleepObserver.observe(&notifySleep);
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

        if (!setUBXMode())
            recordCriticalError(UBloxInitFailed); // Don't halt the boot if saving the config fails, but do report the bug

        concurrency::PeriodicTask::setup(); // We don't start our periodic task unless we actually found the device

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

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int UBloxGPS::prepareSleep(void *unused)
{
    if (isConnected)
        ublox.powerOff();

    return 0;
}

void UBloxGPS::doTask()
{
    if (isConnected) {
        // Consume all characters that have arrived

        uint8_t fixtype = 3; // If we are only using the RX pin, assume we have a 3d fix

        // if using i2c or serial look too see if any chars are ready
        ublox.checkUblox(); // See if new data is available. Process bytes as they come in.

        // If we don't have a fix (a quick check), don't try waiting for a solution)
        // Hmmm my fix type reading returns zeros for fix, which doesn't seem correct, because it is still sptting out positions
        // turn off for now
        uint16_t maxWait = i2cAddress ? 300 : 0; // If using i2c we must poll with wait
        fixtype = ublox.getFixType(maxWait);
        DEBUG_MSG("GPS fix type %d\n", fixtype);

        // DEBUG_MSG("sec %d\n", ublox.getSecond());
        // DEBUG_MSG("lat %d\n", ublox.getLatitude());

        // any fix that has time

        if (ublox.getT(maxWait)) {
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
        }

        latitude = ublox.getLatitude(0);
        longitude = ublox.getLongitude(0);
        altitude = ublox.getAltitudeMSL(0) / 1000; // in mm convert to meters
        dop = ublox.getPDOP(0); // PDOP (an accuracy metric) is reported in 10^2 units so we have to scale down when we use it
        heading = ublox.getHeading(0);
        numSatellites = ublox.getSIV(0);

        // bogus lat lon is reported as 0 or 0 (can be bogus just for one)
        // Also: apparently when the GPS is initially reporting lock it can output a bogus latitude > 90 deg!
        hasValidLocation =
            (latitude != 0) && (longitude != 0) && (latitude <= 900000000 && latitude >= -900000000) && (numSatellites > 0);

        // we only notify if position has changed due to a new fix
        if ((fixtype >= 3 && fixtype <= 4) && ublox.getP(maxWait)) // rd fixes only
        {
            if (hasValidLocation) {
                wantNewLocation = false;
                // ublox.powerOff();
            }
        } else // we didn't get a location update, go back to sleep and hope the characters show up
            wantNewLocation = true;

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status =
            meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop, heading, numSatellites);
        newStatus.notifyObservers(&status);
    }

    // Once we have sent a location once we only poll the GPS rarely, otherwise check back every 10s until we have something
    // over the serial
    setPeriod(hasValidLocation && !wantNewLocation ? 30 * 1000 : 10 * 1000);
}

void UBloxGPS::startLock()
{
    DEBUG_MSG("Looking for GPS lock\n");
    wantNewLocation = true;
    setPeriod(1);
}
