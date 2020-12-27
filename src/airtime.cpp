#include "airtime.h"
#include <Arduino.h>

#define periodsToLog 48

// A reminder that there are 3600 seconds in an hour so I don't have
// to keep googling it.
//   This can be changed to a smaller number to speed up testing.
//
uint32_t secondsPerPeriod = 3600;
uint32_t lastMillis = 0;
uint32_t secSinceBoot = 0;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint16_t periodTX[periodsToLog];
    uint16_t periodRX[periodsToLog];
    uint16_t periodRX_ALL[periodsToLog];
    uint8_t lastPeriodIndex;
} airtimes;

void logAirtime(reportTypes reportType, uint32_t airtime_ms)
{

    if (reportType == TX_LOG) {
        airtimes.periodTX[0] = airtimes.periodTX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_LOG) {
        airtimes.periodRX[0] = airtimes.periodRX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_ALL_LOG) {
        airtimes.periodRX_ALL[0] = airtimes.periodRX_ALL[0] + round(airtime_ms / 1000);
    } else {
        // Unknown report type
    }
}

uint8_t currentPeriodIndex()
{
    return ((getSecondsSinceBoot() / secondsPerPeriod) % periodsToLog);
}

void airtimeCalculator()
{
    if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        secSinceBoot++;
        if (airtimes.lastPeriodIndex != currentPeriodIndex()) {
            for (int i = periodsToLog - 2; i >= 0; --i) {
                airtimes.periodTX[i + 1] = airtimes.periodTX[i];
                airtimes.periodRX[i + 1] = airtimes.periodRX[i];
                airtimes.periodRX_ALL[i + 1] = airtimes.periodRX_ALL[i];
            }
            airtimes.periodTX[0] = 0;
            airtimes.periodRX[0] = 0;
            airtimes.periodRX_ALL[0] = 0;

            airtimes.lastPeriodIndex = currentPeriodIndex();
        }
    }
}

uint16_t *airtimeReport(reportTypes reportType)
{
    // currentHourIndexReset();

    if (reportType == TX_LOG) {
        return airtimes.periodTX;
    } else if (reportType == RX_LOG) {
        return airtimes.periodRX;
    } else if (reportType == RX_ALL_LOG) {
        return airtimes.periodRX_ALL;
    }
    return 0;
}

uint8_t getPeriodsToLog()
{
    return periodsToLog;
}

uint32_t getSecondsPerPeriod()
{
    return secondsPerPeriod;
}

uint32_t getSecondsSinceBoot()
{
    return secSinceBoot;
}
