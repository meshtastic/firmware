#include "airtime.h"
#include <Arduino.h>

#define hoursToLog 48

// Here for convience and to avoid magic numbers.
uint16_t secondsPerHour = 3600;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint16_t hourTX[hoursToLog];
    uint16_t hourRX[hoursToLog];
    uint16_t hourRX_ALL[hoursToLog];
    uint8_t lastHourIndex;
} airtimes;

void logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    currentHourIndexReset();

    if (reportType == TX_LOG) {
        airtimes.hourTX[currentHourIndex()] = airtimes.hourTX[currentHourIndex()] + round(airtime_ms / 1000);
    } else if (reportType == RX_LOG) {
        airtimes.hourRX[currentHourIndex()] = airtimes.hourRX[currentHourIndex()] + round(airtime_ms / 1000);
    } else if (reportType == RX_ALL_LOG) {
        airtimes.hourRX_ALL[currentHourIndex()] = airtimes.hourRX_ALL[currentHourIndex()] + round(airtime_ms / 1000);
    } else {
        // Unknown report type
        
    }
}

// This will let us easily switch away from using millis at some point.
//  todo: Don't use millis, instead maintain our own count of time since
//        boot in seconds.
uint32_t secondsSinceBoot()
{
    return millis() / 1000;
}

uint8_t currentHourIndex()
{
    // return ((secondsSinceBoot() - (secondsSinceBoot() / (hoursToLog * secondsPerHour))) / secondsPerHour);
    return ((secondsSinceBoot() / secondsPerHour) % hoursToLog);
}

// currentHourIndexReset() should be called every time we receive a packet to log (either RX or TX)
//   and every time we are asked to report on airtime usage.
void currentHourIndexReset()
{
    if (airtimes.lastHourIndex != currentHourIndex()) {
        airtimes.hourTX[currentHourIndex()] = 0;
        airtimes.hourRX[currentHourIndex()] = 0;
        airtimes.hourRX_ALL[currentHourIndex()] = 0;

        airtimes.lastHourIndex = currentHourIndex();
    }
}

uint16_t *airtimeReport(reportTypes reportType)
{
    static uint16_t array[hoursToLog];

    currentHourIndexReset();

    for (int i = 0; i < hoursToLog; i++) {
        if (reportType == TX_LOG) {
            array[i] = airtimes.hourTX[(airtimes.lastHourIndex + i) % hoursToLog];
        } else if (reportType == RX_LOG) {
            array[i] = airtimes.hourRX[(airtimes.lastHourIndex + i) % hoursToLog];
        } else if (reportType == RX_ALL_LOG) {
            array[i] = airtimes.hourRX_ALL[(airtimes.lastHourIndex + i) % hoursToLog];
        } else {
            // Unknown report type
            return array;
        }
    }

    return array;
}