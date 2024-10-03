#include "airtime.h"
#include "NodeDB.h"
#include "configuration.h"

AirTime *airTime = NULL;

// Don't read out of this directly. Use the helper functions.

uint32_t air_period_tx[PERIODS_TO_LOG];
uint32_t air_period_rx[PERIODS_TO_LOG];

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{

    if (reportType == TX_LOG) {
        LOG_DEBUG("Packet transmitted : %ums\n", airtime_ms);
        this->airtimes.periodTX[0] = this->airtimes.periodTX[0] + airtime_ms;
        air_period_tx[0] = air_period_tx[0] + airtime_ms;

        this->utilizationTX[this->getPeriodUtilHour()] = this->utilizationTX[this->getPeriodUtilHour()] + airtime_ms;
    } else if (reportType == RX_LOG) {
        LOG_DEBUG("Packet received : %ums\n", airtime_ms);
        this->airtimes.periodRX[0] = this->airtimes.periodRX[0] + airtime_ms;
        air_period_rx[0] = air_period_rx[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        LOG_DEBUG("Packet received (noise?) : %ums\n", airtime_ms);
        this->airtimes.periodRX_ALL[0] = this->airtimes.periodRX_ALL[0] + airtime_ms;
    }

    // Log all airtime type for channel utilization
    this->channelUtilization[this->getPeriodUtilMinute()] = channelUtilization[this->getPeriodUtilMinute()] + airtime_ms;
}

uint8_t AirTime::currentPeriodIndex()
{
    return ((getSecondsSinceBoot() / SECONDS_PER_PERIOD) % PERIODS_TO_LOG);
}

uint8_t AirTime::getPeriodUtilMinute()
{
    return (getSecondsSinceBoot() / 10) % CHANNEL_UTILIZATION_PERIODS;
}

uint8_t AirTime::getPeriodUtilHour()
{
    return (getSecondsSinceBoot() / 60) % MINUTES_IN_HOUR;
}

void AirTime::airtimeRotatePeriod()
{

    if (this->airtimes.lastPeriodIndex != this->currentPeriodIndex()) {
        LOG_DEBUG("Rotating airtimes to a new period = %u\n", this->currentPeriodIndex());

        for (int i = PERIODS_TO_LOG - 2; i >= 0; --i) {
            this->airtimes.periodTX[i + 1] = this->airtimes.periodTX[i];
            this->airtimes.periodRX[i + 1] = this->airtimes.periodRX[i];
            this->airtimes.periodRX_ALL[i + 1] = this->airtimes.periodRX_ALL[i];

            air_period_tx[i + 1] = this->airtimes.periodTX[i];
            air_period_rx[i + 1] = this->airtimes.periodRX[i];
        }

        this->airtimes.periodTX[0] = 0;
        this->airtimes.periodRX[0] = 0;
        this->airtimes.periodRX_ALL[0] = 0;

        air_period_tx[0] = 0;
        air_period_rx[0] = 0;

        this->airtimes.lastPeriodIndex = this->currentPeriodIndex();
    }
}

uint32_t *AirTime::airtimeReport(reportTypes reportType)
{

    if (reportType == TX_LOG) {
        return this->airtimes.periodTX;
    } else if (reportType == RX_LOG) {
        return this->airtimes.periodRX;
    } else if (reportType == RX_ALL_LOG) {
        return this->airtimes.periodRX_ALL;
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
        // LOG_DEBUG("ChanUtilArray %u %u\n", i, this->channelUtilization[i]);
    }

    return (float(sum) / float(CHANNEL_UTILIZATION_PERIODS * 10 * 1000)) * 100;
}

float AirTime::utilizationTXPercent()
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < MINUTES_IN_HOUR; i++) {
        sum += this->utilizationTX[i];
    }

    return (float(sum) / float(MS_IN_HOUR)) * 100;
}

bool AirTime::isTxAllowedChannelUtil(bool polite)
{
    uint8_t percentage = (polite ? polite_channel_util_percent : max_channel_util_percent);
    if (channelUtilizationPercent() < percentage) {
        return true;
    } else {
        LOG_WARN("Channel utilization is >%d percent. Skipping this opportunity to send.\n", percentage);
        return false;
    }
}

bool AirTime::isTxAllowedAirUtil()
{
    if (!config.lora.override_duty_cycle && myRegion->dutyCycle < 100) {
        if (utilizationTXPercent() < myRegion->dutyCycle * polite_duty_cycle_percent / 100) {
            return true;
        } else {
            LOG_WARN("Tx air utilization is >%f percent. Skipping this opportunity to send.\n",
                     myRegion->dutyCycle * polite_duty_cycle_percent / 100);
            return false;
        }
    }
    return true;
}

// Get the amount of minutes we have to be silent before we can send again
uint8_t AirTime::getSilentMinutes(float txPercent, float dutyCycle)
{
    float newTxPercent = txPercent;
    for (int8_t i = MINUTES_IN_HOUR - 1; i >= 0; --i) {
        newTxPercent -= ((float)this->utilizationTX[i] / (MS_IN_MINUTE * MINUTES_IN_HOUR / 100));
        if (newTxPercent < dutyCycle)
            return MINUTES_IN_HOUR - 1 - i;
    }

    return MINUTES_IN_HOUR;
}

AirTime::AirTime() : concurrency::OSThread("AirTime"), airtimes({}) {}

int32_t AirTime::runOnce()
{
    secSinceBoot++;

    uint8_t utilPeriod = this->getPeriodUtilMinute();
    uint8_t utilPeriodTX = this->getPeriodUtilHour();

    if (firstTime) {

        // Init utilizationTX window to all 0
        for (uint32_t i = 0; i < MINUTES_IN_HOUR; i++) {
            this->utilizationTX[i] = 0;
        }

        // Init channelUtilization window to all 0
        for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++) {
            this->channelUtilization[i] = 0;
        }

        // Init airtime windows to all 0
        for (int i = 0; i < PERIODS_TO_LOG; i++) {
            this->airtimes.periodTX[i] = 0;
            this->airtimes.periodRX[i] = 0;
            this->airtimes.periodRX_ALL[i] = 0;

            // air_period_tx[i] = 0;
            // air_period_rx[i] = 0;
        }

        firstTime = false;
        lastUtilPeriod = utilPeriod;
    } else {
        this->airtimeRotatePeriod();

        // Reset the channelUtilization window when we roll over
        if (lastUtilPeriod != utilPeriod) {
            lastUtilPeriod = utilPeriod;

            this->channelUtilization[utilPeriod] = 0;
        }

        if (lastUtilPeriodTX != utilPeriodTX) {
            lastUtilPeriodTX = utilPeriodTX;

            this->utilizationTX[utilPeriodTX] = 0;
        }
    }
    /*
        LOG_DEBUG("utilPeriodTX %d TX Airtime %3.2f%\n", utilPeriodTX, airTime->utilizationTXPercent());
        for (uint32_t i = 0; i < MINUTES_IN_HOUR; i++) {
            LOG_DEBUG(
                "%d,", this->utilizationTX[i]
                );
        }
        LOG_DEBUG("\n");
    */
    return (1000 * 1);
}