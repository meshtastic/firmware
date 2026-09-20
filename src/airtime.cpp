#include "airtime.h"
#include "NodeDB.h"
#include "UptimeClock.h"
#include "configuration.h"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <string.h>

AirTime *airTime = NULL;

AirTime *AirTime::Held::armReentryCheck(AirTime *a)
{
#ifdef AIRTIME_REENTRY_CHECK
    // Before the lock: a nested take blocks forever, so a later check would never run.
    assert(!a->reentryFlag);
    a->reentryFlag = true;
#endif
    return a;
}

AirTime::Held::~Held()
{
#ifdef AIRTIME_REENTRY_CHECK
    owner->reentryFlag = false;
#else
    (void)owner;
#endif
}

// --- the lock-free core -------------------------------------------------------------------------
// Every method here requires the lock, and says so in its signature. None can take it: Windows has
// no lock to reach.

/// Credit [startMs, endMs) to a modular ring of `nSlots` buckets of `periodMs`, giving each bucket
/// only the part of the span that fell inside it. Anything older than the ring is dropped: it is
/// outside every window this ring can answer for.
static void addSpanned(uint32_t *slots, uint8_t nSlots, uint32_t periodMs, uint64_t startMs, uint64_t endMs)
{
    if (endMs <= startMs)
        return;

    // The clamp is not optional: it is all that stands between a future preset whose packet outlasts
    // the whole ring and a wrapped write. It never fires on any preset shipping today.
    const uint64_t windowMs = (uint64_t)nSlots * periodMs;
    if (endMs > windowMs && startMs < endMs - windowMs)
        startMs = endMs - windowMs;

    for (uint64_t t = startMs; t < endMs;) {
        const uint64_t bucketEnd = ((t / periodMs) + 1) * (uint64_t)periodMs;
        const uint64_t hi = bucketEnd < endMs ? bucketEnd : endMs;
        slots[(t / periodMs) % nSlots] += (uint32_t)(hi - t);
        t = hi;
    }
}

void AirTime::Windows::logAirtime(reportTypes reportType, uint32_t airtime_ms, const Held &held)
{
    // A packet may be logged immediately after waking from light sleep. Sync first so the packet is
    // counted against wall time, not a stale awake-time bucket - and, since syncNow() clears every
    // bucket the elapsed time crossed, so that the split below cannot write its tail into a slot
    // that is about to be zeroed.
    syncNow(held);

    // A packet occupied the air from when it started, not all at once when it finished: a LONG_SLOW
    // frame runs 14.164 s, longer than a whole channel-utilisation bucket, so crediting it whole to
    // the completing bucket lets a bucket hold more than its own period and the window report more
    // than its own length.
    // A packet that completed sooner after boot than its own airtime overlapped boot itself. Only
    // the part after boot is inside any window this node can answer for, so clamp the start there
    // rather than sliding the span forward into buckets that have not happened yet - syncNow()
    // clears those on arrival, which loses the airtime and drags the reading down.
    const uint64_t endMs = (uint64_t)this->secSinceBoot * 1000u;
    const uint64_t startMs = endMs > airtime_ms ? endMs - airtime_ms : 0;

    // The caller logs, once the lock is released.
    if (reportType == TX_LOG) {
        // airtimes.period* is shift-ordered rather than a ring, so a split would have to write slot
        // 1 and collide with rotate-on-crossing. Deliberately left whole: 14.164 s misplaced in a
        // 3600 s bucket is 0.4% of a figure that only feeds the HTTP report.
        this->airtimes.periodTX[0] = this->airtimes.periodTX[0] + airtime_ms;
        if (endMs > 0)
            addSpanned(this->utilizationTX, TXUTIL_SLOTS, TXUTIL_PERIOD_MS, startMs, endMs);
        else // logged at uptime 0: no span to spread, and a bucket may hold at most its own period
            this->utilizationTX[this->getPeriodUtilHour(held)] += std::min(airtime_ms, (uint32_t)TXUTIL_PERIOD_MS);
    } else if (reportType == RX_LOG) {
        this->airtimes.periodRX[0] = this->airtimes.periodRX[0] + airtime_ms;
    } else if (reportType == RX_ALL_LOG) {
        this->airtimes.periodRX_ALL[0] = this->airtimes.periodRX_ALL[0] + airtime_ms;
    }

    // Log all airtime type for channel utilization
    if (endMs > 0)
        addSpanned(this->channelUtilization, CHANNEL_UTILIZATION_SLOTS, CHANUTIL_PERIOD_MS, startMs, endMs);
    else
        this->channelUtilization[this->getPeriodUtilMinute(held)] += std::min(airtime_ms, (uint32_t)CHANUTIL_PERIOD_MS);
}

uint8_t AirTime::Windows::getPeriodUtilMinute(const Held &)
{
    return (secSinceBoot / 10) % CHANNEL_UTILIZATION_SLOTS;
}

uint8_t AirTime::Windows::getPeriodUtilHour(const Held &)
{
    return (secSinceBoot / 60) % TXUTIL_SLOTS;
}

uint32_t AirTime::Windows::phaseMs(uint32_t periodSecs) const
{
    return (this->secSinceBoot % periodSecs) * 1000u + this->msInSec;
}

/// The slot `k` periods before `cur`, without relying on unsigned wraparound landing anywhere
/// useful: `cur - k` underflows long before the modulus sees it.
static inline uint8_t slotBack(uint32_t cur, uint32_t k, uint8_t nSlots)
{
    return (uint8_t)((cur + nSlots - (k % nSlots)) % nSlots);
}

/// Sum of the window: the N most recent buckets whole, plus the part of the one N back that has not
/// yet expired. Coverage is then (N-1)p + phase + (p - phase) = Np exactly, which is what the
/// denominator has always claimed.
static uint32_t windowSum(const uint32_t *slots, uint8_t nWindow, uint8_t nSlots, uint32_t periodMs, uint32_t cur,
                          uint32_t phaseMs)
{
    uint32_t sum = 0;
    for (uint32_t k = 0; k < nWindow; k++)
        sum += slots[slotBack(cur, k, nSlots)];

    const uint32_t expiring = slots[slotBack(cur, nWindow, nSlots)];
    sum += (uint32_t)(((uint64_t)expiring * (periodMs - phaseMs)) / periodMs);
    return sum;
}

void AirTime::Windows::syncNow(const Held &held)
{
    // Monotonic uptime, not RTC/network time: a user, GPS, or NTP clock change must not move
    // airtime accounting. Pure read; the main loop publishes the wrap carry it derives from.
    // Taken in milliseconds so the sub-second remainder is available to the interpolation; the one
    // division here is the one getUptimeSecs() was performing internally anyway.
    //
    // getMillisMonotonic() is not ISR-safe (UptimeClock.h): lock-free std::atomic is not guaranteed
    // on every supported toolchain. logAirtime() reaches this from onNotify(), the deferred worker
    // rather than a raw ISR, so it qualifies - do not let it migrate into an ISR later.
    const uint64_t nowMs = Time::getMillisMonotonic();
    const uint32_t nowSecs = (uint32_t)(nowMs / 1000u);
    // Before the early return below: the sub-second phase moves even when the second does not, and
    // a stale phase would step the interpolation weight once a second instead of continuously.
    this->msInSec = (uint16_t)(nowMs - (uint64_t)nowSecs * 1000u);

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
        // Hand the count to runOnce() rather than tracing each crossing here: this runs under
        // the lock, and a UART write would stall every other caller waiting on it.
        this->rotationsPendingLog += elapsedAirtimePeriods;
        for (uint32_t h = 0; h < elapsedAirtimePeriods; h++) {
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
    // Fold one reading per crossed bucket, each before that bucket is cleared, so one delayed sync
    // lands where the same number of 10 s syncs would have. Bounded: clearing every slot empties it.
    const uint32_t steppedUtilPeriods = std::min<uint32_t>(elapsedUtilPeriods, CHANNEL_UTILIZATION_SLOTS);
    for (uint32_t i = 1; i <= steppedUtilPeriods; i++) {
        foldChannelUtil(channelUtilizationPercentRaw(held), 1, held);
        this->channelUtilization[((oldSecSinceBoot / 10) + i) % CHANNEL_UTILIZATION_SLOTS] = 0;
    }
    // Anything past a full window is elapsed time against an already-empty ring, so it folds as
    // idle in closed form rather than looping over a sleep that may have lasted days.
    foldChannelUtil(0.0f, elapsedUtilPeriods - steppedUtilPeriods, held);

    // TX utilization is a rolling 60-minute view used by duty-cycle checks.
    uint32_t elapsedUtilTXPeriods = (this->secSinceBoot / 60) - (oldSecSinceBoot / 60);
    if (elapsedUtilTXPeriods >= TXUTIL_SLOTS) {
        memset(this->utilizationTX, 0, sizeof(this->utilizationTX));
    } else {
        for (uint32_t i = 1; i <= elapsedUtilTXPeriods; i++) {
            this->utilizationTX[((oldSecSinceBoot / 60) + i) % TXUTIL_SLOTS] = 0;
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

float AirTime::Windows::channelUtilizationPercentRaw(const Held &)
{
    const uint32_t sum = windowSum(this->channelUtilization, CHANNEL_UTILIZATION_PERIODS, CHANNEL_UTILIZATION_SLOTS,
                                   CHANUTIL_PERIOD_MS, this->secSinceBoot / 10, phaseMs(10));

    return (float(sum) / float(CHANNEL_UTILIZATION_PERIODS * CHANUTIL_PERIOD_MS)) * 100;
}

float AirTime::Windows::channelUtilizationPercent(const Held &held)
{
    // Gate decisions should see buckets that have decayed across light-sleep time.
    syncNow(held);

    return channelUtilizationPercentRaw(held);
}

void AirTime::Windows::foldChannelUtil(float sample, uint32_t steps, const Held &)
{
    if (steps == 0)
        return;

    if (!hasChannelUtilSample) {
        // Seed from the first reading, or a node booting onto a busy channel reports it quiet
        // for a whole time constant.
        channelUtilAvg = sample;
        hasChannelUtilSample = true;
        steps--;
    }

    if (steps > 0) {
        // Integer power by squaring; powf would link ~1.9 KB of float libm for this one call.
        float base = 1.0f - 1.0f / float(CHANNEL_UTILIZATION_EMA_DIVISOR);
        float retained = 1.0f;
        for (uint32_t e = steps; e != 0; e >>= 1) {
            if (e & 1)
                retained *= base;
            base *= base;
        }
        channelUtilAvg = sample + (channelUtilAvg - sample) * retained;
    }
}

float AirTime::Windows::smoothedChannelUtilizationPercent(const Held &held)
{
    syncNow(held);

    // Nothing folded yet before the first bucket crossing, and 0 would read as an idle channel
    // rather than as no data.
    return hasChannelUtilSample ? channelUtilAvg : channelUtilizationPercentRaw(held);
}

uint32_t AirTime::Windows::utilizationTXMsec(const Held &)
{
    return windowSum(this->utilizationTX, MINUTES_IN_HOUR, TXUTIL_SLOTS, TXUTIL_PERIOD_MS, this->secSinceBoot / SECONDS_IN_MINUTE,
                     phaseMs(SECONDS_IN_MINUTE));
}

float AirTime::Windows::utilizationTXPercent(const Held &held)
{
    // Duty-cycle checks use this value, so keep it current even outside the periodic thread.
    syncNow(held);

    return (float(utilizationTXMsec(held)) / float(MS_IN_HOUR)) * 100;
}

/// The hour's allowance in whole milliseconds. Both admission and the countdown compare against
/// this, so they cannot disagree about where the line is.
static inline uint32_t dutyCycleLimitMs(float dutyCycle)
{
    return (uint32_t)(dutyCycle * (MS_IN_HOUR / 100.0f));
}

bool AirTime::Windows::wouldExceedDutyCycle(uint32_t proposedMs, float dutyCycle, const Held &held)
{
    syncNow(held);
    return utilizationTXMsec(held) + proposedMs > dutyCycleLimitMs(dutyCycle);
}

// Minutes of silence until the hour's TX plus the proposed packet is under the limit. The oldest bucket
// is the one after the current; the proposal is not in the ring and never sheds, it counts in full.
uint8_t AirTime::Windows::getSilentMinutes(float dutyCycle, uint32_t proposedMs, const Held &held)
{
    syncNow(held);

    // `<=` is the exact complement of wouldExceedDutyCycle()'s `>`.
    const uint32_t limitMs = dutyCycleLimitMs(dutyCycle);
    const uint32_t cur = this->secSinceBoot / SECONDS_IN_MINUTE;
    const uint32_t phase = phaseMs(SECONDS_IN_MINUTE);
    uint32_t sum = utilizationTXMsec(held) + proposedMs;

    for (uint8_t m = 0; m < MINUTES_IN_HOUR; m++) {
        if (sum <= limitMs)
            return m;

        // A minute of silence does not shed one whole bucket: the straddler moves along, so it
        // sheds what is left of the expiring bucket and the leading part of the one behind it.
        // Truncating each term rounds the answer UP, which is the safe direction for "you can send
        // again in %d mins".
        const uint32_t behind = this->utilizationTX[slotBack(cur, MINUTES_IN_HOUR - m - 1, TXUTIL_SLOTS)];
        const uint32_t expiring = this->utilizationTX[slotBack(cur, MINUTES_IN_HOUR - m, TXUTIL_SLOTS)];
        const uint32_t shed = (uint32_t)(((uint64_t)behind * phase) / TXUTIL_PERIOD_MS) +
                              (uint32_t)(((uint64_t)expiring * (TXUTIL_PERIOD_MS - phase)) / TXUTIL_PERIOD_MS);
        sum = sum > shed ? sum - shed : 0;
    }

    return MINUTES_IN_HOUR;
}

// --- the locking shell --------------------------------------------------------------------------
// Each takes the lock exactly once and delegates. Nothing below calls another method on `this`.

void AirTime::logAirtime(reportTypes reportType, uint32_t airtime_ms)
{
    {
        Held held(this);
        w.logAirtime(reportType, airtime_ms, held);
    }

    // Outside the lock: DEBUG_PORT.log() blocks on a UART write, and `lock` is a plain binary
    // semaphore with no priority inheritance, so holding it here would stall the radio thread.
    if (reportType == TX_LOG) {
        LOG_DEBUG("Packet TX: %ums", airtime_ms);
    } else if (reportType == RX_LOG) {
        LOG_DEBUG("Packet RX: %ums", airtime_ms);
    } else if (reportType == RX_ALL_LOG) {
        LOG_DEBUG("Packet RX (noise?) : %ums", airtime_ms);
    }
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

float AirTime::smoothedChannelUtilizationPercent()
{
    Held held(this);
    return w.smoothedChannelUtilizationPercent(held);
}

float AirTime::utilizationTXPercent()
{
    Held held(this);
    return w.utilizationTXPercent(held);
}

// These lock like everything else, because they call the core rather than the public accessors.
// Both read under the lock and warn after it, for the reason logAirtime() does.
bool AirTime::isTxAllowedChannelUtil(bool polite)
{
    uint8_t percentage = (polite ? polite_channel_util_percent : max_channel_util_percent);
    float utilization;
    {
        Held held(this);
        utilization = w.channelUtilizationPercent(held);
    }

    if (utilization < percentage)
        return true;
    LOG_WARN("Ch. util >%d%%. Skip send", percentage);
    return false;
}

bool AirTime::isRoutineBroadcastAllowed()
{
    float effectiveDutyCycle = getEffectiveDutyCycle();
    if (!config.lora.override_duty_cycle && effectiveDutyCycle < 100) {
        const float share = effectiveDutyCycle * routine_broadcast_share_percent / 100;
        // No packet exists yet, so admit the widest frame the preset can carry. Rounding up is the
        // safe direction, and on the fast presets it is a fraction of a percent of the share.
        const uint32_t widestMs = getMaxPacketAirtimeMsec();
        bool exceeds;
        {
            Held held(this);
            exceeds = w.wouldExceedDutyCycle(widestMs, share, held);
        }

        if (!exceeds)
            return true;
        LOG_WARN("Routine share of duty cycle (%.2f%%) spent, skip broadcast", share);
        return false;
    }
    return true;
}

bool AirTime::wouldExceedDutyCycle(uint32_t proposedMs, float dutyCycle)
{
    Held held(this);
    return w.wouldExceedDutyCycle(proposedMs, dutyCycle, held);
}

uint8_t AirTime::getSilentMinutes(float dutyCycle, uint32_t proposedMs)
{
    Held held(this);
    return w.getSilentMinutes(dutyCycle, proposedMs, held);
}

AirTime::AirTime() : concurrency::OSThread("AirTime") {}

int32_t AirTime::runOnce()
{
    uint32_t rotations;
    {
        Held held(this);
        w.syncNow(held);
        rotations = w.rotationsPendingLog;
        w.rotationsPendingLog = 0;
    }

    // Outside the lock, for the reason logAirtime() gives. Any caller can cross an hour, but only
    // this thread reports it, so a crossing raised elsewhere is traced at most one tick late.
    if (rotations > 0) {
        LOG_DEBUG("Rotate airtimes, crossed %u hour(s)", rotations);
    }

    return (1000 * 1);
}
