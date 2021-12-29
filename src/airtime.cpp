#include "airtime.h"
#include "NodeDB.h"
#include "configuration.h"

#define PERIODS_TO_LOG 24

AirTime *airTime;

// Don't read out of this directly. Use the helper functions.
struct airtimeStruct {
    uint32_t periodTX[PERIODS_TO_LOG];     // AirTime transmitted
    uint32_t periodRX[PERIODS_TO_LOG];     // AirTime received and repeated (Only valid mesh packets)
    uint32_t periodRX_ALL[PERIODS_TO_LOG]; // AirTime received regardless of valid mesh packet. Could include noise.
    uint8_t lastPeriodIndex;
} airtimes;

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    if (reportType == TX_LOG) {
        DEBUG_MSG("AirTime - Packet transmitted : %ums\n", airtime_ms);
        airtimes.periodTX[0] = airtimes.periodTX[0] + airtime_ms;
        myNodeInfo.air_period_tx[0] = myNodeInfo.air_period_tx[0] + airtime_ms;
    } else if (reportType == RX_LOG) {
        DEBUG_MSG("AirTime - Packet received : %ums\n", airtime_ms);
        airtimes.periodRX[0] = airtimes.periodRX[0] + airtime_ms;
        myNodeInfo.air_period_rx[0] = myNodeInfo.air_period_rx[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        DEBUG_MSG("AirTime - Packet received (noise?) : %ums\n", airtime_ms);
        airtimes.periodRX_ALL[0] = airtimes.periodRX_ALL[0] + airtime_ms;
    }

    uint8_t channelUtilPeriod = (getSecondsSinceBoot() / 10) % CHANNEL_UTILIZATION_PERIODS;
    this->channelUtilization[channelUtilPeriod] = channelUtilization[channelUtilPeriod] + airtime_ms;
}

uint8_t AirTime::currentPeriodIndex()
{
    return ((getSecondsSinceBoot() / SECONDS_PER_PERIOD) % PERIODS_TO_LOG);
}

void AirTime::airtimeRotatePeriod()
{

    if (airtimes.lastPeriodIndex != currentPeriodIndex()) {
        DEBUG_MSG("Rotating airtimes to a new period = %u\n", currentPeriodIndex());

        for (int i = PERIODS_TO_LOG - 2; i >= 0; --i) {
            airtimes.periodTX[i + 1] = airtimes.periodTX[i];
            airtimes.periodRX[i + 1] = airtimes.periodRX[i];
            airtimes.periodRX_ALL[i + 1] = airtimes.periodRX_ALL[i];

            myNodeInfo.air_period_tx[i + 1] = myNodeInfo.air_period_tx[i];
            myNodeInfo.air_period_rx[i + 1] = myNodeInfo.air_period_rx[i];
        }
        airtimes.periodTX[0] = 0;
        airtimes.periodRX[0] = 0;
        airtimes.periodRX_ALL[0] = 0;

        myNodeInfo.air_period_tx[0] = 0;
        myNodeInfo.air_period_rx[0] = 0;

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

uint8_t AirTime::getPeriodsToLog()
{
    return PERIODS_TO_LOG;
}

uint32_t AirTime::getSecondsPerPeriod()
{
    return SECONDS_PER_PERIOD;
}

uint32_t AirTime::getSecondsSinceBoot()
{
    return this->secSinceBoot;
}

float AirTime::channelUtilizationPercent()
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++) {
        sum += this->channelUtilization[i];
        // DEBUG_MSG("ChanUtilArray %u %u\n", i, this->channelUtilization[i]);
    }

    return (float(sum) / float(CHANNEL_UTILIZATION_PERIODS * 10 * 1000)) * 100;
}

AirTime::AirTime() : concurrency::OSThread("AirTime") {}

int32_t AirTime::runOnce()
{
    secSinceBoot++;

    uint8_t utilPeriod = (getSecondsSinceBoot() / 10) % CHANNEL_UTILIZATION_PERIODS;

    if (firstTime) {
        airtimeRotatePeriod();

        for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++) {
            this->channelUtilization[i] = 0;
        }

        firstTime = false;
        lastUtilPeriod = utilPeriod;

    } else {

        // Reset the channelUtilization window when we roll over
        if (lastUtilPeriod != utilPeriod) {
            lastUtilPeriod = utilPeriod;

            this->channelUtilization[utilPeriod] = 0;
        }

        // Update channel_utilization every second.
        myNodeInfo.channel_utilization = airTime->channelUtilizationPercent();
    }

    return (1000 * 1);
}