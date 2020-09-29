#include "NMEAGPS.h"
#include "configuration.h"


static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

bool NMEAGPS::setup()
{
#ifdef PIN_GPS_PPS
    // pulse per second
    // FIXME - move into shared GPS code
    pinMode(PIN_GPS_PPS, INPUT);
#endif

    return true;
}

void NMEAGPS::loop()
{
    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        // DEBUG_MSG("%c", c);
        bool isValid = reader.encode(c);

        // if we have received valid NMEA claim we are connected
        if (isValid)
            isConnected = true;
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();
    bool mustPublishUpdate = false;
    if ((now - lastUpdateMsec) > 30 * 1000 && !wantNewLocation) {
        // Ugly hack for now - limit update checks to once every 30 secs
        setWantLocation(true);
        mustPublishUpdate =
            true; // Even if we don't have an update this time, we at least want to occasionally publish the current state
    }

    // Only bother looking at GPS state if we are interested in what it has to say
    if (wantNewLocation) {
        auto ti = reader.time;
        auto d = reader.date;
        if (ti.isUpdated() && ti.isValid() && d.isValid()) {
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
            perhapsSetRTC(t);
        }

        uint8_t fixtype = reader.fixQuality();
        hasValidLocation = ((fixtype >= 1) && (fixtype <= 5));

        if (reader.location.isUpdated()) {
            lastUpdateMsec = now;

            if (reader.altitude.isValid())
                altitude = reader.altitude.meters();

            if (reader.location.isValid()) {
                auto loc = reader.location.value();
                latitude = toDegInt(loc.lat);
                longitude = toDegInt(loc.lng);

                // Once we get a location we no longer desperately want an update
                setWantLocation(false);
            }
            // Diminution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
            if (reader.hdop.isValid()) {
                dop = reader.hdop.value();
            }
            if (reader.course.isValid()) {
                heading =
                    reader.course.value() * 1e3; // Scale the heading (in degrees * 10^-2) to match the expected degrees * 10^-5
            }
            if (reader.satellites.isValid()) {
                numSatellites = reader.satellites.value();
            }

            // expect gps pos lat=37.520825, lon=-122.309162, alt=158
            DEBUG_MSG("new NMEA GPS pos lat=%f, lon=%f, alt=%d, hdop=%g, heading=%f\n", latitude * 1e-7, longitude * 1e-7,
                      altitude, dop * 1e-2, heading * 1e-5);
            mustPublishUpdate = true;
        }

        if (mustPublishUpdate) {
            // Notify any status instances that are observing us
            const meshtastic::GPSStatus status =
                meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop, heading, numSatellites);
            newStatus.notifyObservers(&status);
        }
    }
}