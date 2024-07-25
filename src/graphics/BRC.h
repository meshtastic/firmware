#pragma once

#include "GPSStatus.h"
#include "gps/GeoCoord.h"
#include "graphics/Screen.h"

using namespace meshtastic;

const int32_t BRC_LATI= (40.786969 * 1e7);
const int32_t BRC_LONI = (-119.204101 * 1e7);
const double BRC_LATF = 40.786969;
const double BRC_LONF = -119.204101;
const double BRC_NOON = 1.5;
const double RAD_TO_HOUR = (6.0/3.14159);
const double METER_TO_FEET = 3.28084;

static char* BRCAddress(int32_t lat, int32_t lon)
{
    static char addrStr[20];

    float bearingToMan =
                GeoCoord::bearing(BRC_LATF, BRC_LONF, DegD(lat), DegD(lon)) * RAD_TO_HOUR;
    bearingToMan += 12.0 - BRC_NOON;
    while (bearingToMan > 12.0) {bearingToMan -= 12.0;}
    uint8_t hour = (uint8_t)(bearingToMan);
    uint8_t minute = (uint8_t)((bearingToMan - hour) * 60.0);
    hour %= 12;
    if (hour == 0) {hour = 12;}

    float d =
                GeoCoord::latLongToMeter(BRC_LATF, BRC_LONF, DegD(lat), DegD(lon)) * METER_TO_FEET;

    if (bearingToMan > 1.75  && bearingToMan < 10.25) {
        const char* street = NULL;
        float dist = 0;
        // tuple of center of street, width to count as on the street, street name
        for (auto&& m : {
                        std::tuple<const float, const float, const char*>{2500,50, "Esp"}, // Esp's center is 2500ft  from man, start showing Esp 50ft from center of street.
                        std::tuple<const float, const float, const char*>{2940,220, "A"}, // Streets are 40ft wide; ESP to A is 400ft
                        std::tuple<const float, const float, const char*>{2940+290,145, "B"}, // A->B is 250ft block + 40ft street
                        std::tuple<const float, const float, const char*>{2940+290*2,145, "C"},
                        std::tuple<const float, const float, const char*>{2940+290*3,145, "D"},
                        std::tuple<const float, const float, const char*>{2940+290*4,145, "E"},
                        std::tuple<const float, const float, const char*>{2940+290*4+490,245, "F"}, // E->F is 450ft
                        std::tuple<const float, const float, const char*>{2940+290*5+490,145, "G"},
                        std::tuple<const float, const float, const char*>{2940+290*6+490,145, "H"},
                        std::tuple<const float, const float, const char*>{2940+290*7+490,145, "I"},
                        std::tuple<const float, const float, const char*>{2940+290*7+490+190,95, "J"}, // I-J & J-K are 150ft
                        std::tuple<const float, const float, const char*>{2940+290*7+490+190*2,95, "K"},
                        std::tuple<const float, const float, const char*>{2940+290*7+490+190*2+75,0, 0} }) {
            float c = std::get<0>(m);
            float w = std::get<1>(m);
            if (d > c-w) {
                street = std::get<2>(m);
                dist = d -c;
            } else {
                break;
            }
        }
        if (street) {
            snprintf(addrStr, sizeof(addrStr), "%d:%02d & %s %dft", hour, minute, street, int(dist));
            return addrStr;
        }

    }

    snprintf(addrStr, sizeof(addrStr), "%d:%02d & %dft", hour, minute, (uint32_t)d);
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
