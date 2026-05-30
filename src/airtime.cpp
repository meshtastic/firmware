#include "airtime.h"
#include "NodeDB.h"
#include "configuration.h"
#include <string.h>

AirTime *airTime = NULL;

// Don't read out of this directly. Use the helper functions.

uint32_t air_period_tx[PERIODS_TO_LOG];
uint32_t air_period_rx[PERIODS_TO_LOG];

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    // A packet may be logged immediately after waking from light sleep. Sync first so
    // the packet is counted in the current wall-time bucket, not a stale awake-time bucket.
    syncNow();

    if (reportType == TX_LOG) {
        LOG_DEBUG("Packet TX: %ums", airtime_ms);
        this->airtimes.periodTX[0] = this->airtimes.periodTX[0] + airtime_ms;
        air_period_tx[0] = air_period_tx[0] + airtime_ms;

        this->utilizationTX[this->getPeriodUtilHour()] = this->utilizationTX[this->getPeriodUtilHour()] + airtime_ms;
    } else if (reportType == RX_LOG) {
        LOG_DEBUG("Packet RX: %ums", airtime_ms);
        this->airtimes.periodRX[0] = this->airtimes.periodRX[0] + airtime_ms;
        air_period_rx[0] = air_period_rx[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        LOG_DEBUG("Packet RX (noise?) : %ums", airtime_ms);
        this->airtimes.periodRX_ALL[0] = this->airtimes.periodRX_ALL[0] + airtime_ms;
    }

    // Log all airtime type for channel utilization
    this->channelUtilization[this->getPeriodUtilMinute()] = channelUtilization[this->getPeriodUtilMinute()] + airtime_ms;
}

uint8_t AirTime::currentPeriodIndex()
{
    return ((secSinceBoot / SECONDS_PER_PERIOD) % PERIODS_TO_LOG);
}

uint8_t AirTime::getPeriodUtilMinute()
{
    return (secSinceBoot / 10) % CHANNEL_UTILIZATION_PERIODS;
}

uint8_t AirTime::getPeriodUtilHour()
{
    return (secSinceBoot / 60) % MINUTES_IN_HOUR;
}

void AirTime::airtimeRotatePeriod()
{
    // Preserve the public helper while keeping all rotation logic in one monotonic-time path.
    syncNow();
}

void AirTime::syncNow()
{
    // Use monotonic uptime rather than RTC/network time; user, GPS, or NTP clock changes
    // must not move airtime accounting backward or forward.
    uint32_t nowMsec = millis();

    if (firstTime) {
        memset(this->utilizationTX, 0, sizeof(this->utilizationTX));
        memset(this->channelUtilization, 0, sizeof(this->channelUtilization));
        memset(this->airtimes.periodTX, 0, sizeof(this->airtimes.periodTX));
        memset(this->airtimes.periodRX, 0, sizeof(this->airtimes.periodRX));
        memset(this->airtimes.periodRX_ALL, 0, sizeof(this->airtimes.periodRX_ALL));
        memset(air_period_tx, 0, sizeof(air_period_tx));
        memset(air_period_rx, 0, sizeof(air_period_rx));

        this->secSinceBoot = nowMsec / 1000;
        // Keep the checkpoint on a whole-second boundary so elapsedSecs advances predictably.
        this->lastSyncMsec = nowMsec - (nowMsec % 1000);
        this->lastUtilPeriod = this->getPeriodUtilMinute();
        this->lastUtilPeriodTX = this->getPeriodUtilHour();
        this->airtimes.lastPeriodIndex = this->currentPeriodIndex();
        firstTime = false;
        return;
    }

    uint32_t elapsedMsec = nowMsec - this->lastSyncMsec;
    uint32_t elapsedSecs = elapsedMsec / 1000;
    if (elapsedSecs == 0) {
        return;
    }

    uint32_t oldSecSinceBoot = this->secSinceBoot;
    this->secSinceBoot += elapsedSecs;
    this->lastSyncMsec += elapsedSecs * 1000;

    // Historical airtime reports use 1-hour buckets. If multiple hours elapsed while
    // asleep, rotate each crossed bucket or clear the whole report window.
    uint32_t elapsedAirtimePeriods = (this->secSinceBoot / SECONDS_PER_PERIOD) - (oldSecSinceBoot / SECONDS_PER_PERIOD);
    if (elapsedAirtimePeriods >= PERIODS_TO_LOG) {
        memset(this->airtimes.periodTX, 0, sizeof(this->airtimes.periodTX));
        memset(this->airtimes.periodRX, 0, sizeof(this->airtimes.periodRX));
        memset(this->airtimes.periodRX_ALL, 0, sizeof(this->airtimes.periodRX_ALL));
        memset(air_period_tx, 0, sizeof(air_period_tx));
        memset(air_period_rx, 0, sizeof(air_period_rx));
    } else {
        while (elapsedAirtimePeriods-- > 0) {
            LOG_DEBUG("Rotate airtimes to a new period = %u", this->currentPeriodIndex());
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
        }
    }
    this->airtimes.lastPeriodIndex = this->currentPeriodIndex();

    // Channel utilization is a rolling 60-second view split into six 10-second buckets.
    // Clear every bucket crossed while asleep so old airtime decays by real elapsed time.
    uint32_t elapsedUtilPeriods = (this->secSinceBoot / 10) - (oldSecSinceBoot / 10);
    if (elapsedUtilPeriods >= CHANNEL_UTILIZATION_PERIODS) {
        memset(this->channelUtilization, 0, sizeof(this->channelUtilization));
    } else {
        for (uint32_t i = 1; i <= elapsedUtilPeriods; i++) {
            this->channelUtilization[((oldSecSinceBoot / 10) + i) % CHANNEL_UTILIZATION_PERIODS] = 0;
        }
    }
    this->lastUtilPeriod = this->getPeriodUtilMinute();

    // TX utilization is a rolling 60-minute view used by duty-cycle checks.
    uint32_t elapsedUtilTXPeriods = (this->secSinceBoot / 60) - (oldSecSinceBoot / 60);
    if (elapsedUtilTXPeriods >= MINUTES_IN_HOUR) {
        memset(this->utilizationTX, 0, sizeof(this->utilizationTX));
    } else {
        for (uint32_t i = 1; i <= elapsedUtilTXPeriods; i++) {
            this->utilizationTX[((oldSecSinceBoot / 60) + i) % MINUTES_IN_HOUR] = 0;
        }
    }
    this->lastUtilPeriodTX = this->getPeriodUtilHour();
}

uint32_t *AirTime::airtimeReport(reportTypes reportType)
{
    // Reports may be requested before runOnce() executes after wake.
    syncNow();

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
    // Keep HTTP/debug reporting aligned with the same monotonic clock used by the buckets.
    syncNow();
    return this->secSinceBoot;
}

float AirTime::channelUtilizationPercent()
{
    // Gate decisions should see buckets that have decayed across light-sleep time.
    syncNow();

    uint32_t sum = 0;
    for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++) {
        sum += this->channelUtilization[i];
    }

    return (float(sum) / float(CHANNEL_UTILIZATION_PERIODS * 10 * 1000)) * 100;
}

float AirTime::utilizationTXPercent()
{
    // Duty-cycle checks use this value, so keep it current even outside the periodic thread.
    syncNow();

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
        LOG_WARN("Ch. util >%d%%. Skip send", percentage);
        return false;
    }
}

bool AirTime::isTxAllowedAirUtil()
{
    float effectiveDutyCycle = getEffectiveDutyCycle();
    if (!config.lora.override_duty_cycle && effectiveDutyCycle < 100) {
        if (utilizationTXPercent() < effectiveDutyCycle * polite_duty_cycle_percent / 100) {
            return true;
        } else {
            LOG_WARN("TX air util. >%f%%. Skip send", effectiveDutyCycle * polite_duty_cycle_percent / 100);
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
    syncNow();
    return (1000 * 1);
}
