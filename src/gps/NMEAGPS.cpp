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

void NMEAGPS::loop()
{
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        // Serial.write(c);
        reader.encode(c);
    }

    uint32_t now = millis();
    if ((now - lastUpdateMsec) > 20 * 1000) { // Ugly hack for now - limit update checks to once every 20 secs (but still consume
                                              // serial chars at whatever rate)
        lastUpdateMsec = now;

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

            isConnected = true; // we seem to have a real GPS (but not necessarily a lock)
        }

        uint8_t fixtype = reader.fixQuality();
        hasValidLocation = ((fixtype >= 1) && (fixtype <= 5));

        if (reader.location.isUpdated()) {
            if (reader.altitude.isValid())
                altitude = reader.altitude.meters();

            if (reader.location.isValid()) {
                auto loc = reader.location.value();
                latitude = toDegInt(loc.lat);
                longitude = toDegInt(loc.lng);
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
            DEBUG_MSG("new NMEA GPS pos lat=%f, lon=%f, alt=%d, hdop=%f, heading=%f\n", latitude * 1e-7, longitude * 1e-7,
                      altitude, dop * 1e-2, heading * 1e-5);
        }

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status =
            meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop, heading, numSatellites);
        newStatus.notifyObservers(&status);
    }
}