#include "NMEAWPL.h"

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
 */

uint printWPL(char *buf, const Position &pos, const char *name)
{
    uint len = sprintf(buf, "$GNWPL,%07.2f,%c,%08.2f,%c,%s", pos.latitude_i * 1e-5, pos.latitude_i < 0 ? 'S' : 'N', pos.longitude_i * 1e-5, pos.longitude_i < 0 ? 'W' : 'E', name);
    uint chk = 0;
    for (uint i = 1; i < len; i++) {
        chk ^= buf[i];
    }
    len += sprintf(buf + len, "*%02X\r\n", chk);
    return len;
}
