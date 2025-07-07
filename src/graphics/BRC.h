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

/*
# python code to generate the StreetInfo

esp_center = 2500
street_info = [
  # name, width, preceeding block depth
  ('Esp', 40, 60), # block size is fake
  ('A',   30, 400),
  ('B',   30, 250),
  ('C',   30, 250),
  ('D',   30, 250),
  ('E',   40, 250),
  ('F',   30, 450), # E-F block is exra deep
  ('G',   30, 250),
  ('H',   30, 250),
  ('I',   30, 250),
  ('J',   30, 150),
  ('K',   50, 150),
]

street_center = esp_center - street_info[0][1] //2 - street_info[0][2]
last_center = esp_center
for (name, street_width, block_width) in street_info:
    offset = (street_width + block_width) // 2
    street_center += street_width //2 + block_width

    dia = street_center * 2
    dist = street_center - last_center

    print(f"{{{street_center}, {offset}, \"{name}\"}},\t// +{dist}ft\tdia: {dia:,}ft")

    last_center = street_center
    street_center += street_width //2

street_center += 50 # extra buffer after the edge of k to include walk-in camping parking
print(f"{{{street_center}, 0, nullptr}},\t// +{street_center-last_center}ft")
*/

static const StreetInfo streets[] = {
    {2500, 50, "Esp"},	// +0ft	dia: 5,000ft
    {2935, 215, "A"},	// +435ft	dia: 5,870ft
    {3215, 140, "B"},	// +280ft	dia: 6,430ft
    {3495, 140, "C"},	// +280ft	dia: 6,990ft
    {3775, 140, "D"},	// +280ft	dia: 7,550ft
    {4060, 145, "E"},	// +285ft	dia: 8,120ft
    {4545, 240, "F"},	// +485ft	dia: 9,090ft
    {4825, 140, "G"},	// +280ft	dia: 9,650ft
    {5105, 140, "H"},	// +280ft	dia: 10,210ft
    {5385, 140, "I"},	// +280ft	dia: 10,770ft
    {5565, 90, "J"},	// +180ft	dia: 11,130ft
    {5755, 100, "K"},	// +190ft	dia: 11,510ft
    {5830, 0, nullptr},	// +75ft
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
