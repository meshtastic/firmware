#include "SatellitesRenderer.h"
#if defined(TTGO_T_ECHO_PLUS) && defined(USE_EINK) && !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#include "graphics/ScreenFonts.h"
#include <algorithm>
#include <cstdio>

namespace graphics::SatellitesRenderer
{
static const char *systemName(uint8_t s)
{
    switch (s) {
    case TINYGPS_GNSS_GPS:
        return "GPS";
    case TINYGPS_GNSS_GLONASS:
        return "GLO";
    case TINYGPS_GNSS_GALILEO:
        return "GAL";
    case TINYGPS_GNSS_BEIDOU:
        return "BDS";
    case TINYGPS_GNSS_QZSS:
        return "QZS";
    default:
        return "---";
    }
}

static const char *powerStateName(GPSPowerState state)
{
    switch (state) {
    case GPS_ACTIVE:
        return "ACTIVE";
    case GPS_IDLE:
        return "IDLE";
    case GPS_SOFTSLEEP:
        return "SLEEP";
    case GPS_HARDSLEEP:
        return "SLEEP";
    case GPS_OFF:
        return "OFF";
    default:
        return "?";
    }
}

void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int16_t w = display->getWidth();
    const int16_t h = display->getHeight();

    if (!gps || !gps->isConnected()) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + w / 2, y + h / 2, "GNSS not connected");
        return;
    }

    const GPSPowerState state = gps->getPowerState();
    const bool live = state == GPS_ACTIVE;

    // While ACTIVE, every number below is age-filtered live receiver data.
    // While IDLE/SLEEPING, show the last checksum-valid snapshot instead.
    // This is deliberate: sleeping time must never turn a valid old snapshot
    // into an apparent live "0 satellites" condition.
    const uint16_t used = live ? gps->getSatellitesUsed() : gps->getSatellitesUsedSnapshot();
    const uint16_t tracked = live ? gps->getSatellitesTracked() : gps->getSatellitesTrackedSnapshot();
    const uint16_t view = live ? gps->getSatellitesInView() : gps->getSatellitesInViewSnapshot();
    const uint8_t fix = live ? gps->getGsaFixType() : gps->getGsaFixTypeSnapshot();
    const uint16_t pdop = live ? gps->getGsaPDOP() : gps->getGsaPDOPSnapshot();
    const uint16_t hdop = live ? gps->getGsaHDOP() : gps->getGsaHDOPSnapshot();
    const uint16_t vdop = live ? gps->getGsaVDOP() : gps->getGsaVDOPSnapshot();

    char stateLine[28];
    snprintf(stateLine, sizeof(stateLine), "GNSS %s%s", powerStateName(state), live ? "" : " / LAST");
    display->drawString(x + 2, y + 1, stateLine);

    char top[42];
    snprintf(top, sizeof(top), "Used:%u Trk:%u View:%u", (unsigned)used, (unsigned)tracked, (unsigned)view);
    display->drawString(x + 2, y + 14, top);

    char systems[46];
    snprintf(systems, sizeof(systems), "GPS:%u GLO:%u BDS:%u QZS:%u",
             (unsigned)(live ? gps->getSatellitesUsedBySystem(TINYGPS_GNSS_GPS)
                             : gps->getSatellitesUsedBySystemSnapshot(TINYGPS_GNSS_GPS)),
             (unsigned)(live ? gps->getSatellitesUsedBySystem(TINYGPS_GNSS_GLONASS)
                             : gps->getSatellitesUsedBySystemSnapshot(TINYGPS_GNSS_GLONASS)),
             (unsigned)(live ? gps->getSatellitesUsedBySystem(TINYGPS_GNSS_BEIDOU)
                             : gps->getSatellitesUsedBySystemSnapshot(TINYGPS_GNSS_BEIDOU)),
             (unsigned)(live ? gps->getSatellitesUsedBySystem(TINYGPS_GNSS_QZSS)
                             : gps->getSatellitesUsedBySystemSnapshot(TINYGPS_GNSS_QZSS)));
    display->drawString(x + 2, y + 28, systems);

    char dop[48];
    snprintf(dop, sizeof(dop), "Fix:%u P:%u H:%u V:%u", (unsigned)fix, (unsigned)pdop, (unsigned)hdop, (unsigned)vdop);
    display->drawString(x + 2, y + 41, dop);

    display->drawHorizontalLine(x + 2, y + 54, w - 4);
    // '*' means this SVID is explicitly listed by the checksum-valid GSA
    // sentence as used in the navigation solution.
    display->drawString(x + 2, y + 56, live ? "U SYS ID EL AZ SNR" : "LAST U SYS ID EL AZ SNR");

    const auto *sats = gps->getTrackedSatellites();
    const size_t cap = gps->getTrackedSatelliteCapacity();
    const TinyGPSTrackedSattelites *list[TINYGPS_MAX_SATS];
    size_t count = 0;

    for (size_t i = 0; i < cap && count < TINYGPS_MAX_SATS; ++i) {
        const bool include = live ? gps->isTrackedSatelliteFresh(sats[i]) : (sats[i].prn != 0);
        if (include)
            list[count++] = &sats[i];
    }

    std::sort(list, list + count, [live](const auto *a, const auto *b) {
        const bool aUsed = live ? gps->isSatelliteUsed(*a) : gps->isSatelliteUsedSnapshot(*a);
        const bool bUsed = live ? gps->isSatelliteUsed(*b) : gps->isSatelliteUsedSnapshot(*b);
        if (aUsed != bUsed)
            return aUsed > bUsed;
        if (a->tracked != b->tracked)
            return a->tracked > b->tracked;
        if (a->strength != b->strength)
            return a->strength > b->strength;
        if (a->system != b->system)
            return a->system < b->system;
        return a->prn < b->prn;
    });

    int16_t yy = y + 68;
    const int16_t bottom = y + h - 4;

    if (count == 0) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        if (live)
            display->drawString(x + w / 2, yy + 8, "Waiting for fresh GSV");
        else if (state == GPS_OFF)
            display->drawString(x + w / 2, yy + 8, "GNSS is off");
        else
            display->drawString(x + w / 2, yy + 8, "No saved GSV snapshot");
        return;
    }

    for (size_t i = 0; i < count && yy + 11 <= bottom; ++i) {
        char row[40];
        const bool usedInFix = live ? gps->isSatelliteUsed(*list[i]) : gps->isSatelliteUsedSnapshot(*list[i]);
        const char usedMark = usedInFix ? '*' : ' ';
        if (list[i]->tracked)
            snprintf(row, sizeof(row), "%c %-3s %3u %2u %3u %3u", usedMark, systemName(list[i]->system),
                     (unsigned)list[i]->prn, (unsigned)list[i]->elevation, (unsigned)list[i]->azimuth,
                     (unsigned)list[i]->strength);
        else
            snprintf(row, sizeof(row), "%c %-3s %3u %2u %3u  --", usedMark, systemName(list[i]->system),
                     (unsigned)list[i]->prn, (unsigned)list[i]->elevation, (unsigned)list[i]->azimuth);

        display->drawString(x + 2, yy, row);
        yy += 13;
    }
}
} // namespace graphics::SatellitesRenderer
#endif
