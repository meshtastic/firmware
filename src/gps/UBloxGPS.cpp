#include "UBloxGPS.h"
#include "sleep.h"
#include <assert.h>

UBloxGPS::UBloxGPS() : PeriodicTask()
{
    notifySleepObserver.observe(&notifySleep);
}

bool UBloxGPS::setup()
{
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
        DEBUG_MSG("Connected to UBLOX GPS successfully\n");

        bool factoryReset = false;
        bool ok;
        if (factoryReset) {
            // It is useful to force back into factory defaults (9600baud, NEMA to test the behavior of boards that don't have
            // GPS_TX connected)
            ublox.factoryReset();
            delay(3000);
            isConnected = ublox.begin(_serial_gps);
            DEBUG_MSG("Factory reset success=%d\n", isConnected);
            ok = ublox.saveConfiguration(3000);
            assert(ok);
            return false;
        } else {
            ok = ublox.setUART1Output(COM_TYPE_UBX, 500); // Use native API
            assert(ok);
            ok = ublox.setNavigationFrequency(1, 500); // Produce 4x/sec to keep the amount of time we stall in getPVT low
            assert(ok);
            // ok = ublox.setAutoPVT(false); // Not implemented on NEO-6M
            // assert(ok);
            // ok = ublox.setDynamicModel(DYN_MODEL_BIKE); // probably PEDESTRIAN but just in case assume bike speeds
            // assert(ok);
            ok = ublox.powerSaveMode(true, 2000); // use power save mode, the default timeout (1100ms seems a bit too tight)
            assert(ok);
        }
        ok = ublox.saveConfiguration(3000);
        assert(ok);

        PeriodicTask::setup(); // We don't start our periodic task unless we actually found the device

        return true;
    } else {
        return false;
    }
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
    uint8_t fixtype = 3; // If we are only using the RX pin, assume we have a 3d fix

    assert(isConnected);

    // Consume all characters that have arrived

    // getPVT automatically calls checkUblox
    ublox.checkUblox(); // See if new data is available. Process bytes as they come in.

    // If we don't have a fix (a quick check), don't try waiting for a solution)
    // Hmmm my fix type reading returns zeros for fix, which doesn't seem correct, because it is still sptting out positions
    // turn off for now
    fixtype = ublox.getFixType(0);
    DEBUG_MSG("GPS fix type %d\n", fixtype);

    // DEBUG_MSG("sec %d\n", ublox.getSecond());
    // DEBUG_MSG("lat %d\n", ublox.getLatitude());

    // any fix that has time
    if (ublox.getT(0)) {
        /* Convert to unix time
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
(midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
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

    if ((fixtype >= 3 && fixtype <= 4) && ublox.getP(0)) // rd fixes only
    {
        // we only notify if position has changed
        latitude = ublox.getLatitude(0);
        longitude = ublox.getLongitude(0);
        altitude = ublox.getAltitude(0) / 1000; // in mm convert to meters
        dop = ublox.getPDOP(0); // PDOP (an accuracy metric) is reported in 10^2 units so we have to scale down when we use it

        // bogus lat lon is reported as 0 or 0 (can be bogus just for one)
        // Also: apparently when the GPS is initially reporting lock it can output a bogus latitude > 90 deg!
        hasValidLocation = (latitude != 0) && (longitude != 0) && (latitude <= 900000000 && latitude >= -900000000);
        if (hasValidLocation) {
            wantNewLocation = false;
            notifyObservers(NULL);
            // ublox.powerOff();
        }
    } else // we didn't get a location update, go back to sleep and hope the characters show up
        wantNewLocation = true;

    // Notify any status instances that are observing us
    const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop);
    newStatus.notifyObservers(&status);

    // Once we have sent a location once we only poll the GPS rarely, otherwise check back every 1s until we have something over
    // the serial
    setPeriod(hasValidLocation && !wantNewLocation ? 30 * 1000 : 10 * 1000);
}

void UBloxGPS::startLock()
{
    DEBUG_MSG("Looking for GPS lock\n");
    wantNewLocation = true;
    setPeriod(1);
}
