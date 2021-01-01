#include "airtime.h"
#include <Arduino.h>

#define periodsToLog 48

AirTime airTime;

// A reminder that there are 3600 seconds in an hour so I don't have
// to keep googling it.
//   This can be changed to a smaller number to speed up testing.
//
uint32_t secondsPerPeriod = 3600;
uint32_t lastMillis = 0;
uint32_t secSinceBoot = 0;

// AirTime at;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint16_t periodTX[periodsToLog];
    uint16_t periodRX[periodsToLog];
    uint16_t periodRX_ALL[periodsToLog];
    uint8_t lastPeriodIndex;
} airtimes;

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    DEBUG_MSG("Packet - logAirtime()\n");

    if (reportType == TX_LOG) {
        DEBUG_MSG("Packet transmitted = %u\n", (uint32_t)round(airtime_ms / 1000));
        airtimes.periodTX[0] = airtimes.periodTX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_LOG) {
        DEBUG_MSG("Packet received = %u\n", (uint32_t)round(airtime_ms / 1000));
        airtimes.periodRX[0] = airtimes.periodRX[0] + round(airtime_ms / 1000);
    } else if (reportType == RX_ALL_LOG) {
        DEBUG_MSG("Packet received (noise?) = %u\n", (uint32_t)round(airtime_ms / 1000));
        airtimes.periodRX_ALL[0] = airtimes.periodRX_ALL[0] + round(airtime_ms / 1000);
    } else {
        // Unknown report type
    }
}

uint8_t currentPeriodIndex()
{
    return ((getSecondsSinceBoot() / secondsPerPeriod) % periodsToLog);
}

void airtimeRotatePeriod()
{

    if (airtimes.lastPeriodIndex != currentPeriodIndex()) {
        DEBUG_MSG("Rotating airtimes to a new period = %u\n", currentPeriodIndex());

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

AirTime::AirTime() : concurrency::OSThread("AirTime") {}

int32_t AirTime::runOnce()
{
    DEBUG_MSG("AirTime::runOnce()\n");

    airtimeRotatePeriod();
    secSinceBoot++;

    return 1000;
}