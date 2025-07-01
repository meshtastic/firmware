#pragma once

#include "GPSStatus.h"
#include "gps/GeoCoord.h"
#include "graphics/Screen.h"

using namespace meshtastic;

const int32_t BRC_LATI= (40.786958 * 1e7);
const int32_t BRC_LONI = (-119.202994 * 1e7);
const double BRC_LATF = 40.786958;
const double BRC_LONF = -119.202994;
const double BRC_NOON = 1.5;
const double RAD_TO_HOUR = (6.0/3.14159);
const double METER_TO_FEET = 3.28084;

// Pre-calculated street data for performance
struct StreetInfo {
    float center;
    float width;
    const char* name;
};

static const StreetInfo streets[] = {
    {2500, 50, "Esp"},
    {2940, 220, "A"},
    {2940+290, 145, "B"},
    {2940+290*2, 145, "C"},
    {2940+290*3, 145, "D"},
    {2940+290*4, 145, "E"},
    {2940+290*4+490, 245, "F"},
    {2940+290*5+490, 145, "G"},
    {2940+290*6+490, 145, "H"},
    {2940+290*7+490, 145, "I"},
    {2940+290*7+490+190, 95, "J"},
    {2940+290*7+490+190*2, 95, "K"},
    {2940+290*7+490+190*2+75, 0, nullptr}
};

static char* BRCAddress(int32_t lat, int32_t lon)
{
    thread_local static char addrStr[20];

    double unitMultiplier = 1.0 / METER_TO_FEET;
    const char* unit = "m";
    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
        unitMultiplier = 1.0;
        unit = "ft";
    }

    float bearingToMan =
                GeoCoord::bearing(BRC_LATF, BRC_LONF, DegD(lat), DegD(lon)) * RAD_TO_HOUR;
    bearingToMan += 12.0 - BRC_NOON;
    while (bearingToMan > 12.0) {bearingToMan -= 12.0;}
    uint8_t hour = (uint8_t)(bearingToMan);
    uint8_t minute = (uint8_t)((bearingToMan - hour) * 60.0);
    hour %= 12;
    if (hour == 0) {hour = 12;}

    // In imperial units because that is how golden spike data is provided.
    float d =
                GeoCoord::latLongToMeter(BRC_LATF, BRC_LONF, DegD(lat), DegD(lon)) * METER_TO_FEET;

    if (bearingToMan > 1.75  && bearingToMan < 10.25) {
        const char* street = nullptr;
        float dist = 0;
        // Find the appropriate street based on distance
        for (const auto& s : streets) {
            if (d > s.center - s.width) {
                street = s.name;
                dist = d - s.center;
            } else {
                break;
            }
        }
        if (street) {
            snprintf(addrStr, sizeof(addrStr), "%d:%02d & %s %d%s", hour, minute, street, int(dist * unitMultiplier), unit);
            return addrStr;
        }

    }

    snprintf(addrStr, sizeof(addrStr), "%d:%02d & %d%s", hour, minute, int(d * unitMultiplier), unit);
    return addrStr;
}


static void drawBRCAddress(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    if (!gps->getIsConnected() && !config.position.fixed_position) {
        // displayLine = "No GPS Module";
        // display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        // displayLine = "No GPS Lock";
        // display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {
        auto displayLine = BRCAddress(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()));
        display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}
