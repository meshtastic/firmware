#include "SatellitesRenderer.h"
#if defined(TTGO_T_ECHO_PLUS) && defined(USE_EINK) && !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#include "graphics/ScreenFonts.h"
#include <algorithm>
#include <cstdio>

namespace graphics
{
namespace SatellitesRenderer
{

static const char *systemName(uint8_t s)
{
    switch (s) {
    case TINYGPS_GNSS_GPS:
    case TINYGPS_GNSS_MIXED:
        return "GPS";
    case TINYGPS_GNSS_GLONASS:
        return "GLO";
    case TINYGPS_GNSS_BEIDOU:
        return "BDS";
    default:
        return "---";
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

    char top[42];
    snprintf(top, sizeof(top), "Used:%u Trk:%u View:%u", (unsigned)gps->getSatellitesUsed(),
             (unsigned)gps->getSatellitesTracked(), (unsigned)gps->getSatellitesInView());
    display->drawString(x + 2, y + 1, top);

    char systems[46];
    snprintf(systems, sizeof(systems), "GPS:%u GLO:%u BDS:%u", (unsigned)gps->getSatellitesUsedBySystem(TINYGPS_GNSS_GPS),
             (unsigned)gps->getSatellitesUsedBySystem(TINYGPS_GNSS_GLONASS),
             (unsigned)gps->getSatellitesUsedBySystem(TINYGPS_GNSS_BEIDOU));
    display->drawString(x + 2, y + 14, systems);
    display->drawString(x + 2, y + 28, "SYS ID EL  AZ  SNR");
<<<<<<< HEAD
    display->drawHorizontalLine(x + 2, y + 44, w - 4);
=======
    display->drawHorizontalLine(x + 2, y + 43, w - 4);
>>>>>>> b20727418 (feat: Support Elecrow ThinkNode M9 (#10908))

    const auto *sats = gps->getTrackedSatellites();
    const size_t capacity = gps->getTrackedSatelliteCapacity();
    const TinyGPSTrackedSattelites *visible[TINYGPS_MAX_SATS];
    size_t count = 0;

    for (size_t i = 0; i < capacity && count < TINYGPS_MAX_SATS; ++i) {
        if (sats[i].prn != 0) {
            visible[count++] = &sats[i];
        }
    }

    std::sort(visible, visible + count, [](const auto *a, const auto *b) {
        if (a->strength != b->strength)
            return a->strength > b->strength;
        if (a->system != b->system)
            return a->system < b->system;
        return a->prn < b->prn;
    });

<<<<<<< HEAD
    int16_t yy = y + 46;
=======
    int16_t yy = y + 58;
>>>>>>> b20727418 (feat: Support Elecrow ThinkNode M9 (#10908))
    const int16_t bottom = y + h - 12;
    for (size_t i = 0; i < count && yy + 13 <= bottom; ++i) {
        char row[40];
        if (visible[i]->tracked) {
            snprintf(row, sizeof(row), "%-3s %3u %2u %3u %3u", systemName(visible[i]->system), (unsigned)visible[i]->prn,
                     (unsigned)visible[i]->elevation, (unsigned)visible[i]->azimuth, (unsigned)visible[i]->strength);
        } else {
            snprintf(row, sizeof(row), "%-3s %3u %2u %3u  --", systemName(visible[i]->system), (unsigned)visible[i]->prn,
                     (unsigned)visible[i]->elevation, (unsigned)visible[i]->azimuth);
        }

        display->drawString(x + 2, yy, row);
        yy += 12;
    }
}

} // namespace SatellitesRenderer
} // namespace graphics
#endif
