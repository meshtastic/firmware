#include "NMEAGPS.h"
#include "configuration.h"

/*
Helpful translations from the Air530 GPS datasheet

Sat acquision mode
捕获电流值@3.3v 42.6 mA

sat tracking mode
跟踪电流值@3.3v 36.7 mA

Low power mode
低功耗模式@3.3V 0.85 mA
(发送指令:$PGKC051,0)

Super low power mode
超低功耗模式@3.3V 31 uA
(发送指令:$PGKC105,4)

To exit sleep use WAKE pin

Commands to enter sleep
6、Command: 105
进入周期性低功耗模式
Arguments:

Arg1: “0”,正常运行模式 (normal mode)
“1”,周期超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (periodic low power tracking mode - keeps sat positions, use wake to wake up)
“2”,周期低功耗模式 (periodic low power mode)
“4”,直接进入超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (super low power consumption mode immediately, need WAKE to resume)
“8”,自动低功耗模式,可以通过串口唤醒 (automatic low power mode, wake by sending characters to serial port)
“9”, 自动超低功耗跟踪模式,需要拉高 WAKE 来唤醒 (automatic low power tracking when possible, need wake pin to resume)

(Arg 2 & 3 only valid if Arg1 is "1" or "2")
Arg2:运行时间(毫秒),在 Arg1 为 1、2 的周期模式下,此参数起作用
ON time in msecs

Arg3:睡眠时间(毫秒),在 Arg1 为 1、2 的周期模式下,此参数起作用
Sleep time in msecs

Example:
$PGKC105,8*3F<CR><LF>
This will set automatic low power mode with waking when we send chars to the serial port.  Possibly do this as soon as we get a new 
location.  When we wake again in a minute we send a character to wake up.

*/

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
    while (_serial_gps->available() > 0) {
        int c = _serial_gps->read();
        // DEBUG_MSG("%c", c);
        bool isValid = reader.encode(c);

        // if we have received valid NMEA claim we are connected
        if (isValid)
            isConnected = true;
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
            DEBUG_MSG("new NMEA GPS pos lat=%f, lon=%f, alt=%d, hdop=%g, heading=%f\n", latitude * 1e-7, longitude * 1e-7,
                      altitude, dop * 1e-2, heading * 1e-5);
        }

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status =
            meshtastic::GPSStatus(hasLock(), isConnected, latitude, longitude, altitude, dop, heading, numSatellites);
        newStatus.notifyObservers(&status);
    }
}