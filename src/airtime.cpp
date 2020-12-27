#include "airtime.h"
#include <Arduino.h>

#define hoursToLog 48

// A reminder that there are 3600 seconds in an hour so I don't have
// to keep googling it.
//   This can be changed to a smaller number to speed up testing.
//
uint16_t secondsPerHour = 3600;
uint32_t lastMillis = 0;
uint32_t secSinceBoot = 0;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint16_t hourTX[hoursToLog];
    uint16_t hourRX[hoursToLog];
    uint16_t hourRX_ALL[hoursToLog];
    uint8_t lastHourIndex;
} airtimes;

void logAirtime(reportTypes reportType, uint32_t airtime_ms)
{

    if (reportType == TX_LOG) {
        airtimes.hourTX[0] = airtimes.hourTX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_LOG) {
        airtimes.hourRX[0] = airtimes.hourRX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_ALL_LOG) {
        airtimes.hourRX_ALL[0] = airtimes.hourRX_ALL[0] + round(airtime_ms / 1000);
    } else {
        // Unknown report type
    }
}

uint32_t getSecondsSinceBoot()
{
    return secSinceBoot;
}

uint8_t currentHourIndex()
{
    // return ((secondsSinceBoot() - (secondsSinceBoot() / (hoursToLog * secondsPerHour))) / secondsPerHour);
    return ((getSecondsSinceBoot() / secondsPerHour) % hoursToLog);
}

void airtimeCalculator()
{
    if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        secSinceBoot++;
        // DEBUG_MSG("------- lastHourIndex %i currentHourIndex %i\n", airtimes.lastHourIndex, currentHourIndex());
        if (airtimes.lastHourIndex != currentHourIndex()) {
            for (int i = hoursToLog - 2; i >= 0; --i) {
                airtimes.hourTX[i + 1] = airtimes.hourTX[i];
                airtimes.hourRX[i + 1] = airtimes.hourRX[i];
                airtimes.hourRX_ALL[i + 1] = airtimes.hourRX_ALL[i];
            }
            airtimes.hourTX[0] = 0;
            airtimes.hourRX[0] = 0;
            airtimes.hourRX_ALL[0] = 0;

            airtimes.lastHourIndex = currentHourIndex();
        }
    }
}

uint16_t *airtimeReport(reportTypes reportType)
{
    // currentHourIndexReset();

    if (reportType == TX_LOG) {
        return airtimes.hourTX;
    } else if (reportType == RX_LOG) {
        return airtimes.hourRX;
    } else if (reportType == RX_ALL_LOG) {
        return airtimes.hourRX_ALL;
    }
    return 0;
}

uint8_t getHoursToLog()
{
    return hoursToLog;
}