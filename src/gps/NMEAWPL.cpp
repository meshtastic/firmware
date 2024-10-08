#if !MESHTASTIC_EXCLUDE_GPS
#include "NMEAWPL.h"
#include "GeoCoord.h"
#include "RTC.h"
#include <time.h>

/* -------------------------------------------
 *        1       2 3        4 5    6
 *        |       | |        | |    |
 * $--WPL,llll.ll,a,yyyyy.yy,a,c--c*hh<CR><LF>
 *
 * Field Number:
 * 1 Latitude
 * 2 N or S (North or South)
 * 3 Longitude
 * 4 E or W (East or West)
 * 5 Waypoint name
 * 6 Checksum
 * -------------------------------------------
 */

uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_PositionLite &pos, const char *name, bool isCaltopoMode)
{
    GeoCoord geoCoord(pos.latitude_i, pos.longitude_i, pos.altitude);
    char type = isCaltopoMode ? 'P' : 'N';
    uint32_t len = snprintf(buf, bufsz, "$G%cWPL,%02d%07.4f,%c,%03d%07.4f,%c,%s", type, geoCoord.getDMSLatDeg(),
                            (abs(geoCoord.getLatitude()) - geoCoord.getDMSLatDeg() * 1e+7) * 6e-6, geoCoord.getDMSLatCP(),
                            geoCoord.getDMSLonDeg(), (abs(geoCoord.getLongitude()) - geoCoord.getDMSLonDeg() * 1e+7) * 6e-6,
                            geoCoord.getDMSLonCP(), name);
    uint32_t chk = 0;
    for (uint32_t i = 1; i < len; i++) {
        chk ^= buf[i];
    }
    len += snprintf(buf + len, bufsz - len, "*%02X\r\n", chk);
    return len;
}

uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_Position &pos, const char *name, bool isCaltopoMode)
{
    GeoCoord geoCoord(pos.latitude_i, pos.longitude_i, pos.altitude);
    char type = isCaltopoMode ? 'P' : 'N';
    uint32_t len = snprintf(buf, bufsz, "$G%cWPL,%02d%07.4f,%c,%03d%07.4f,%c,%s", type, geoCoord.getDMSLatDeg(),
                            (abs(geoCoord.getLatitude()) - geoCoord.getDMSLatDeg() * 1e+7) * 6e-6, geoCoord.getDMSLatCP(),
                            geoCoord.getDMSLonDeg(), (abs(geoCoord.getLongitude()) - geoCoord.getDMSLonDeg() * 1e+7) * 6e-6,
                            geoCoord.getDMSLonCP(), name);
    uint32_t chk = 0;
    for (uint32_t i = 1; i < len; i++) {
        chk ^= buf[i];
    }
    len += snprintf(buf + len, bufsz - len, "*%02X\r\n", chk);
    return len;
}
/* -------------------------------------------
 *        1         2       3 4       5 6 7  8   9  10 11 12 13  14   15
 *        |         |       | |       | | |  |   |   | |   | |   |    |
 * $--GGA,hhmmss.ss,ddmm.mm,a,ddmm.mm,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh<CR><LF>
 *
 * Field Number:
 *  1 UTC of this position report, hh is hours, mm is minutes, ss.ss is seconds.
 *  2 Latitude
 *  3 N or S (North or South)
 *  4 Longitude
 *  5 E or W (East or West)
 *  6 GPS Quality Indicator (non null)
 *  7 Number of satellites in use, 00 - 12
 *  8 Horizontal Dilution of precision (meters)
 *  9 Antenna Altitude above/below mean-sea-level (geoid) (in meters)
 * 10 Units of antenna altitude, meters
 * 11 Geoidal separation, the difference between the WGS-84 earth ellipsoid and mean-sea-level (geoid), "-" means mean-sea-level
 * below ellipsoid 12 Units of geoidal separation, meters 13 Age of differential GPS data, time in seconds since last SC104 type 1
 * or 9 update, null field when DGPS is not used 14 Differential reference station ID, 0000-1023 15 Checksum
 * -------------------------------------------
 */

uint32_t printGGA(char *buf, size_t bufsz, const meshtastic_Position &pos)
{
    GeoCoord geoCoord(pos.latitude_i, pos.longitude_i, pos.altitude);
    time_t timestamp = pos.timestamp;

    tm *t = gmtime(&timestamp);
    if (getRTCQuality() > 0) { // use the device clock if we got time from somewhere. If not, use the GPS timestamp.
        uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice);
        timestamp = rtc_sec;
        t = gmtime(&timestamp);
    }

    uint32_t len = snprintf(
        buf, bufsz, "$GNGGA,%02d%02d%02d.%02d,%02d%07.4f,%c,%03d%07.4f,%c,%u,%02u,%04u,%04d,%c,%04d,%c,%d,%04d", t->tm_hour,
        t->tm_min, t->tm_sec, pos.timestamp_millis_adjust, geoCoord.getDMSLatDeg(),
        (abs(geoCoord.getLatitude()) - geoCoord.getDMSLatDeg() * 1e+7) * 6e-6, geoCoord.getDMSLatCP(), geoCoord.getDMSLonDeg(),
        (abs(geoCoord.getLongitude()) - geoCoord.getDMSLonDeg() * 1e+7) * 6e-6, geoCoord.getDMSLonCP(), pos.fix_quality,
        pos.sats_in_view, pos.HDOP, geoCoord.getAltitude(), 'M', pos.altitude_geoidal_separation, 'M', 0, 0);

    uint32_t chk = 0;
    for (uint32_t i = 1; i < len; i++) {
        chk ^= buf[i];
    }
    len += snprintf(buf + len, bufsz - len, "*%02X\r\n", chk);
    return len;
}

#endif