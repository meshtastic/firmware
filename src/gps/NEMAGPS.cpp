#include "NEMAGPS.h"
#include "configuration.h"

static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

void NEMAGPS::loop()
{

    while (_serial_gps.available() > 0) {
        int c = _serial_gps.read();
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

        if (reader.location.isUpdated()) {
            if (reader.altitude.isValid())
                altitude = reader.altitude.meters();

            if (reader.location.isValid()) {
                auto loc = reader.location.value();
                latitude = toDegInt(loc.lat);
                longitude = toDegInt(loc.lng);
            }

            // expect gps pos lat=37.520825, lon=-122.309162, alt=158
            DEBUG_MSG("new NEMA GPS pos lat=%f, lon=%f, alt=%d\n", latitude * 1e-7, longitude * 1e-7, altitude);

            hasValidLocation = (latitude != 0) || (longitude != 0); // bogus lat lon is reported as 0,0
            if (hasValidLocation)
                notifyObservers(NULL);
        }
    }
}