#include "configuration.h"
#include "airtime.h"

#define periodsToLog 48

AirTime *airTime;

uint32_t secondsPerPeriod = 3600;
uint32_t lastMillis = 0;
uint32_t secSinceBoot = 0;

// AirTime at;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint32_t periodTX[periodsToLog];     // AirTime transmitted
    uint32_t periodRX[periodsToLog];     // AirTime received and repeated (Only valid mesh packets)
    uint32_t periodRX_ALL[periodsToLog]; // AirTime received regardless of valid mesh packet. Could include noise.
    uint8_t lastPeriodIndex;
} airtimes;

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    if (reportType == TX_LOG) {
        DEBUG_MSG("AirTime - Packet transmitted : %ums\n", airtime_ms);
        airtimes.periodTX[0] = airtimes.periodTX[0] + airtime_ms;
    } else if (reportType == RX_LOG) {
        DEBUG_MSG("AirTime - Packet received : %ums\n", airtime_ms);
        airtimes.periodRX[0] = airtimes.periodRX[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        DEBUG_MSG("AirTime - Packet received (noise?) : %ums\n", airtime_ms);
        airtimes.periodRX_ALL[0] = airtimes.periodRX_ALL[0] + airtime_ms;
    } else {
        DEBUG_MSG("AirTime - Unknown report time. This should never happen!!\n");
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

uint32_t *airtimeReport(reportTypes reportType)
{

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
    //DEBUG_MSG("AirTime::runOnce()\n");

    airtimeRotatePeriod();
    secSinceBoot++;

    /*
        This actually doesn't need to be run once per second but we currently use it for the
        secSinceBoot counter.

        If we have a better counter of how long the device has been online (and not millis())
        then we can change this to something less frequent. Maybe once ever 5 seconds?
    */
    return (1000 * 1);
}