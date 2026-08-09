#include "mesh/cell/cellDiagParse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Locate the line carrying prefix and return the first character after the
// prefix and its colon, skipping spaces. Null when no such line exists.
static const char *findLine(const char *resp, const char *prefix)
{
    if (!resp || !prefix)
        return nullptr;

    size_t plen = strlen(prefix);
    const char *p = resp;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, prefix, plen) == 0) {
            const char *q = p + plen;
            while (*q == ' ')
                q++;
            if (*q == ':') {
                q++;
                while (*q == ' ')
                    q++;
                return q;
            }
        }
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
    return nullptr;
}

// Copy comma-separated field idx out of a single line, unquoted and trimmed.
static bool field(const char *line, int idx, char *buf, size_t bufLen)
{
    if (!line || bufLen == 0)
        return false;

    const char *p = line;
    for (int i = 0; i < idx; i++) {
        while (*p && *p != ',' && *p != '\r' && *p != '\n')
            p++;
        if (*p != ',')
            return false;
        p++;
    }

    while (*p == ' ')
        p++;

    const char *end = p;
    while (*end && *end != ',' && *end != '\r' && *end != '\n')
        end++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    if (end - p >= 2 && *p == '"' && end[-1] == '"') {
        p++;
        end--;
    }

    size_t len = (size_t)(end - p);
    if (len == 0 || len >= bufLen)
        return false;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return true;
}

// Field idx as a non-negative integer, or -1 when absent or not all digits.
static long intField(const char *line, int idx)
{
    char buf[16];
    if (!field(line, idx, buf, sizeof(buf)))
        return -1;
    for (const char *c = buf; *c; c++)
        if (!isdigit((unsigned char)*c))
            return -1;
    return strtol(buf, nullptr, 10);
}

bool parseCesq(const char *resp, int16_t *rsrpDbm, float *rsrqDb)
{
    const char *line = findLine(resp, "+CESQ");
    if (!line)
        return false;

    long rsrq = intField(line, 4);
    long rsrp = intField(line, 5);
    // 3GPP TS 27.007: <rsrq> is 0-34, <rsrp> is 0-97; 255 means not known or not
    // detectable, and anything else outside range is not valid signal data either.
    if (rsrq < 0 || rsrq > 34 || rsrp < 0 || rsrp > 97)
        return false;

    if (rsrpDbm)
        *rsrpDbm = (int16_t)(-140 + rsrp);
    if (rsrqDb)
        *rsrqDb = -20.0f + 0.5f * (float)rsrq;
    return true;
}

int parseCeregStat(const char *resp)
{
    const char *line = findLine(resp, "+CEREG");
    if (!line)
        return -1;
    return (int)intField(line, 1);
}

bool parseBandInd(const char *resp, uint8_t *band, uint8_t *act)
{
    const char *line = findLine(resp, "*BANDIND");
    if (!line)
        return false;

    long b = intField(line, 1);
    long a = intField(line, 2);
    if (b < 0 || b > 255 || a < 0 || a > 255)
        return false;

    if (band)
        *band = (uint8_t)b;
    if (act)
        *act = (uint8_t)a;
    return true;
}

bool parseCbcMillivolts(const char *resp, uint16_t *mv)
{
    const char *line = findLine(resp, "+CBC");
    if (!line)
        return false;

    // SIMCom answers <bcs>,<bcl>,<mV>; the Air780E answers with the millivolts
    // alone. Accept either rather than silently dropping the reading.
    long v = intField(line, 2);
    if (v < 0)
        v = intField(line, 0);

    // The module runs from 3.4-4.2 V; anything outside a generous window around
    // that is a different encoding, not a reading to display.
    if (v < 2000 || v > 5000)
        return false;

    if (mv)
        *mv = (uint16_t)v;
    return true;
}

bool parseCopsName(const char *resp, char *out, size_t outLen)
{
    const char *line = findLine(resp, "+COPS");
    if (!line || !out || outLen == 0)
        return false;

    long format = intField(line, 1);
    if (format != 0 && format != 1) // only the alphanumeric formats carry a name
        return false;

    return field(line, 2, out, outLen);
}

bool parseCopsNumeric(const char *resp, char *out, size_t outLen)
{
    const char *line = findLine(resp, "+COPS");
    if (!line || !out || outLen == 0)
        return false;

    if (intField(line, 1) != 2) // format 2 is the numeric MCC/MNC
        return false;

    char buf[16];
    if (!field(line, 2, buf, sizeof(buf)))
        return false;
    for (const char *c = buf; *c; c++)
        if (!isdigit((unsigned char)*c))
            return false;
    if (strlen(buf) >= outLen)
        return false;

    strcpy(out, buf);
    return true;
}

bool parseSysConfig(const char *resp, uint8_t *mode, uint8_t *acqorder, uint8_t *srvdomain)
{
    const char *line = findLine(resp, "^SYSCONFIG");
    if (!line)
        return false;

    long m = intField(line, 0);
    long a = intField(line, 1);
    long s = intField(line, 3);
    if (m < 0 || m > 255 || a < 0 || a > 255 || s < 0 || s > 255)
        return false;

    if (mode)
        *mode = (uint8_t)m;
    if (acqorder)
        *acqorder = (uint8_t)a;
    if (srvdomain)
        *srvdomain = (uint8_t)s;
    return true;
}
