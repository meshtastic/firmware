#include "airtime.h"
#include "NodeDB.h"
#include "UptimeClock.h"
#include "configuration.h"
#include <assert.h>
#include <string.h>

AirTime *airTime = NULL;

AirTime *AirTime::Held::armReentryCheck(AirTime *a)
{
#ifdef PIO_UNIT_TESTING
    // Before the lock, not after: on hardware a nested take blocks forever, so an assert placed
    // after it would never be reached.
    assert(!a->reentryFlag);
    a->reentryFlag = true;
#endif
    return a;
}

AirTime::Held::~Held()
{
#ifdef PIO_UNIT_TESTING
    owner->reentryFlag = false;
#else
    (void)owner;
#endif
}

// --- the lock-free core -------------------------------------------------------------------------
// Every method here assumes the lock is held, and says so in its signature. None of them can take
// it: Windows has no lock to reach.

void AirTime::Windows::logAirtime(reportTypes reportType, uint32_t airtime_ms, const Held &held)
{
    // A packet may be logged immediately after waking from light sleep. Sync first so
    // the packet is counted in the current wall-time bucket, not a stale awake-time bucket.
    syncNow(held);

    if (reportType == TX_LOG) {
        LOG_DEBUG("Packet TX: %ums", airtime_ms);
        this->airtimes.periodTX[0] = this->airtimes.periodTX[0] + airtime_ms;

        this->utilizationTX[this->getPeriodUtilHour(held)] = this->utilizationTX[this->getPeriodUtilHour(held)] + airtime_ms;
    } else if (reportType == RX_LOG) {
        LOG_DEBUG("Packet RX: %ums", airtime_ms);
        this->airtimes.periodRX[0] = this->airtimes.periodRX[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        LOG_DEBUG("Packet RX (noise?) : %ums", airtime_ms);
        this->airtimes.periodRX_ALL[0] = this->airtimes.periodRX_ALL[0] + airtime_ms;
    }

    // Log all airtime type for channel utilization
    this->channelUtilization[this->getPeriodUtilMinute(held)] = channelUtilization[this->getPeriodUtilMinute(held)] + airtime_ms;
}

uint8_t AirTime::Windows::getPeriodUtilMinute(const Held &)
{
    return (secSinceBoot / 10) % CHANNEL_UTILIZATION_PERIODS;
}

uint8_t AirTime::Windows::getPeriodUtilHour(const Held &)
{
    return (secSinceBoot / 60) % MINUTES_IN_HOUR;
}

void AirTime::Windows::syncNow(const Held &)
{
    // Monotonic uptime, not RTC/network time: a user, GPS, or NTP clock change must not move
    // airtime accounting. Pure read; the main loop publishes the wrap carry it derives from.
    uint32_t nowSecs = Time::getUptimeSecs();

    if (firstTime) {
        memset(this->utilizationTX, 0, sizeof(this->utilizationTX));
        memset(this->channelUtilization, 0, sizeof(this->channelUtilization));
        memset(this->airtimes.periodTX, 0, sizeof(this->airtimes.periodTX));
        memset(this->airtimes.periodRX, 0, sizeof(this->airtimes.periodRX));
        memset(this->airtimes.periodRX_ALL, 0, sizeof(this->airtimes.periodRX_ALL));

        this->secSinceBoot = nowSecs;
        firstTime = false;
        return;
    }

    if (nowSecs == this->secSinceBoot) {
        return;
    }

    uint32_t oldSecSinceBoot = this->secSinceBoot;
    this->secSinceBoot = nowSecs;

    // Historical airtime reports use 1-hour buckets. If multiple hours elapsed while
    // asleep, rotate each crossed bucket or clear the whole report window.
    uint32_t elapsedAirtimePeriods = (this->secSinceBoot / SECONDS_PER_PERIOD) - (oldSecSinceBoot / SECONDS_PER_PERIOD);
    if (elapsedAirtimePeriods >= PERIODS_TO_LOG) {
        memset(this->airtimes.periodTX, 0, sizeof(this->airtimes.periodTX));
        memset(this->airtimes.periodRX, 0, sizeof(this->airtimes.periodRX));
        memset(this->airtimes.periodRX_ALL, 0, sizeof(this->airtimes.periodRX_ALL));
    } else {
        // Counter is the loop variable rather than a separate tally: LOG_DEBUG compiles to nothing
        // under DEBUG_MUTE, which would leave a tally write-only.
        for (uint32_t h = 1; h <= elapsedAirtimePeriods; h++) {
            LOG_DEBUG("Rotate airtimes, crossed hour %u", h);
            for (int i = PERIODS_TO_LOG - 2; i >= 0; --i) {
                this->airtimes.periodTX[i + 1] = this->airtimes.periodTX[i];
                this->airtimes.periodRX[i + 1] = this->airtimes.periodRX[i];
                this->airtimes.periodRX_ALL[i + 1] = this->airtimes.periodRX_ALL[i];
            }

            this->airtimes.periodTX[0] = 0;
            this->airtimes.periodRX[0] = 0;
            this->airtimes.periodRX_ALL[0] = 0;
        }
    }

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

    // TX utilization is a rolling 60-minute view used by duty-cycle checks.
    uint32_t elapsedUtilTXPeriods = (this->secSinceBoot / 60) - (oldSecSinceBoot / 60);
    if (elapsedUtilTXPeriods >= MINUTES_IN_HOUR) {
        memset(this->utilizationTX, 0, sizeof(this->utilizationTX));
    } else {
        for (uint32_t i = 1; i <= elapsedUtilTXPeriods; i++) {
            this->utilizationTX[((oldSecSinceBoot / 60) + i) % MINUTES_IN_HOUR] = 0;
        }
    }
}

bool AirTime::Windows::airtimeReport(reportTypes reportType, uint32_t *out, size_t count, const Held &held)
{
    if (!out || count > PERIODS_TO_LOG)
        return false;

    // Reports may be requested before runOnce() executes after wake.
    syncNow(held);

    const uint32_t *src = nullptr;
    if (reportType == TX_LOG) {
        src = this->airtimes.periodTX;
    } else if (reportType == RX_LOG) {
        src = this->airtimes.periodRX;
    } else if (reportType == RX_ALL_LOG) {
        src = this->airtimes.periodRX_ALL;
    }
    if (!src)
        return false;

    memcpy(out, src, count * sizeof(*out));
    return true;
}

float AirTime::Windows::channelUtilizationPercent(const Held &held)
{
    // Gate decisions should see buckets that have decayed across light-sleep time.
    syncNow(held);

    uint32_t sum = 0;
    for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++) {
        sum += this->channelUtilization[i];
    }

    return (float(sum) / float(CHANNEL_UTILIZATION_PERIODS * 10 * 1000)) * 100;
}

float AirTime::Windows::utilizationTXPercent(const Held &held)
{
    // Duty-cycle checks use this value, so keep it current even outside the periodic thread.
    syncNow(held);

    uint32_t sum = 0;
    for (uint32_t i = 0; i < MINUTES_IN_HOUR; i++) {
        sum += this->utilizationTX[i];
    }

    return (float(sum) / float(MS_IN_HOUR)) * 100;
}

// Get the amount of minutes we have to be silent before we can send again.
// Deliberately does NOT sync, matching the behaviour its characterisation tests pin. Both that and
// the ring traversal below are wrong; see the accuracy TODO at the top of airtime.h.
uint8_t AirTime::Windows::getSilentMinutes(float txPercent, float dutyCycle, const Held &)
{
    float newTxPercent = txPercent;
    for (int8_t i = MINUTES_IN_HOUR - 1; i >= 0; --i) {
        newTxPercent -= ((float)this->utilizationTX[i] / (MS_IN_MINUTE * MINUTES_IN_HOUR / 100));
        if (newTxPercent < dutyCycle)
            return MINUTES_IN_HOUR - 1 - i;
    }

    return MINUTES_IN_HOUR;
}

// --- the locking shell --------------------------------------------------------------------------
// Every one of these takes the lock exactly once and delegates. Nothing below calls anything else
// on `this`, which is what keeps the "locks exactly once" rule checkable by reading.

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    Held held(this);
    w.logAirtime(reportType, airtime_ms, held);
}

void AirTime::airtimeRotatePeriod()
{
    // Preserve the public helper while keeping all rotation logic in one monotonic-time path.
    Held held(this);
    w.syncNow(held);
}

bool AirTime::airtimeReport(reportTypes reportType, uint32_t *out, size_t count)
{
    Held held(this);
    return w.airtimeReport(reportType, out, count, held);
}

uint8_t AirTime::getPeriodsToLog()
{
    return PERIODS_TO_LOG; // a constant: touches no state, so takes no lock
}

uint32_t AirTime::getSecondsPerPeriod()
{
    return SECONDS_PER_PERIOD; // likewise
}

uint32_t AirTime::getSecondsSinceBoot()
{
    // Keep HTTP/debug reporting aligned with the same monotonic clock used by the buckets.
    Held held(this);
    w.syncNow(held);
    return w.secSinceBoot;
}

float AirTime::channelUtilizationPercent()
{
    Held held(this);
    return w.channelUtilizationPercent(held);
}

float AirTime::utilizationTXPercent()
{
    Held held(this);
    return w.utilizationTXPercent(held);
}

// Locks like everything else. Before the core split these could not, because they called the
// public accessors and the lock is not recursive - the one asymmetry a reader had to remember.
bool AirTime::isTxAllowedChannelUtil(bool polite)
{
    uint8_t percentage = (polite ? polite_channel_util_percent : max_channel_util_percent);
    Held held(this);
    if (w.channelUtilizationPercent(held) < percentage) {
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
        Held held(this);
        if (w.utilizationTXPercent(held) < effectiveDutyCycle * polite_duty_cycle_percent / 100) {
            return true;
        } else {
            LOG_WARN("TX air util. >%f%%. Skip send", effectiveDutyCycle * polite_duty_cycle_percent / 100);
            return false;
        }
    }
    return true;
}

uint8_t AirTime::getSilentMinutes(float txPercent, float dutyCycle)
{
    Held held(this);
    return w.getSilentMinutes(txPercent, dutyCycle, held);
}

AirTime::AirTime() : concurrency::OSThread("AirTime") {}

int32_t AirTime::runOnce()
{
    Held held(this);
    w.syncNow(held);
    return (1000 * 1);
}
