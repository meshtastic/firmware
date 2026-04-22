#include "HopScalingModule.h"

#if HAS_VARIABLE_HOPS

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include "mesh-pb-constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
// Module scheduling
constexpr uint32_t INITIAL_DELAY_MS = 30 * 1000UL;    // Startup grace period before first run
constexpr uint32_t RUN_INTERVAL_MS = 5 * 60 * 1000UL; // Emit micro-summary every 5 minutes
// RUNS_PER_HOUR is a public class constant in HopScalingModule.h

// Persistence
// Note: this only needs incrementing if the published arrangement changes. For testing purposes, or prior to widespread release,
// it can stay the same even if the internal layout changes.
constexpr uint32_t HISTOGRAM_STATE_MAGIC = 0x48535432; // 'HST2' — layout v2
constexpr uint8_t HISTOGRAM_STATE_VERSION = 1;
constexpr const char *HISTOGRAM_STATE_FILE = "/prefs/hopScalingState.bin";

#pragma pack(push, 1)
struct PersistedHistogram {
    uint32_t magic;
    uint8_t version;
    uint8_t samplingDenominator;
    uint8_t filteringDenominator;
    uint8_t filterDenomHoldRollsRemaining; // rollHour() calls remaining in the hold; 0 when expired/not active
    uint16_t hashSeed;
    Record entries[HopScalingModule::CAPACITY]; // full 512-byte array; count derived on load
};
#pragma pack(pop)

} // namespace

HopScalingModule *hopScalingModule;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

HopScalingModule::HopScalingModule() : concurrency::OSThread("HopScaling")
{
    clear();
    loadFromDisk();
    setIntervalFromNow(INITIAL_DELAY_MS);
}

void HopScalingModule::clear()
{
    memset(entries, 0, sizeof(entries));
    count = 0;
    samplingDenominator = DENOM_MIN;
    filteringDenominator = DENOM_MIN;
    filteringDenomHoldRollsRemaining = 0;
    lastPerHopCounts = {};
    lastSuggestedHop = MAX_HOP;
    lastPoliteness = POLITENESS_DEFAULT;
    lastTrendStats = {};
    memset(denominatorHistory, DENOM_MIN, sizeof(denominatorHistory));
#ifndef UNIT_TEST
    hashSeed = static_cast<uint16_t>(random());
#else
    hashSeed = 0; // deterministic in unit tests
#endif
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void HopScalingModule::saveToDisk() const
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
    if (FSCom.exists(HISTOGRAM_STATE_FILE)) {
        FSCom.remove(HISTOGRAM_STATE_FILE);
    }
    concurrency::LockGuard g(spiLock);
    auto file = FSCom.open(HISTOGRAM_STATE_FILE, FILE_O_WRITE);
    if (file) {
        PersistedHistogram state{};
        state.magic = HISTOGRAM_STATE_MAGIC;
        state.version = HISTOGRAM_STATE_VERSION;
        state.samplingDenominator = samplingDenominator;
        state.filteringDenominator = filteringDenominator;
        state.filterDenomHoldRollsRemaining = filteringDenomHoldRollsRemaining;
        state.hashSeed = hashSeed;
        // Save all CAPACITY slots; count is reconstructed on load by scanning seenHoursAgo.
        memcpy(state.entries, entries, sizeof(state.entries));
        file.write(reinterpret_cast<const uint8_t *>(&state), sizeof(state));
        file.flush();
        file.close();
        LOG_DEBUG("[HOPSCALE] Saved: count=%u samp=1/%u filt=1/%u holdRollsRemaining=%u", count, samplingDenominator,
                  filteringDenominator, state.filterDenomHoldRollsRemaining);
    } else {
        LOG_WARN("[HOPSCALE] Failed to open %s for write", HISTOGRAM_STATE_FILE);
    }
#endif
}

void HopScalingModule::loadFromDisk()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto file = FSCom.open(HISTOGRAM_STATE_FILE, FILE_O_READ);
    if (!file)
        return;
    PersistedHistogram state{};
    const bool readOk = (file.read(reinterpret_cast<uint8_t *>(&state), sizeof(state)) == sizeof(state));
    file.close();
    if (!readOk || state.magic != HISTOGRAM_STATE_MAGIC || state.version != HISTOGRAM_STATE_VERSION ||
        state.samplingDenominator < DENOM_MIN || state.samplingDenominator > DENOM_MAX ||
        state.filteringDenominator < state.samplingDenominator || state.filteringDenominator > DENOM_MAX) {
        LOG_DEBUG("[HOPSCALE] No valid persisted state (magic=%08x ver=%u), starting fresh", state.magic, state.version);
        return;
    }
    // Derive count by scanning: active entries have seenHoursAgo != 0; pack them to the front.

    uint8_t restored = 0;
    for (uint8_t i = 0; i < CAPACITY && restored < CAPACITY; i++) {
        if (state.entries[i].seenHoursAgo != 0u) {
            entries[restored++] = state.entries[i];
        }
    }
    count = restored;
    samplingDenominator = state.samplingDenominator;
    filteringDenominator = state.filteringDenominator;
    filteringDenomHoldRollsRemaining = state.filterDenomHoldRollsRemaining;
    // denominatorHistory can't be recovered; initialise all slots to filteringDenominator so
    // the first few post-reboot scaledPerHour values use a safe (slightly conservative) multiplier.
    memset(denominatorHistory, filteringDenominator, sizeof(denominatorHistory));
    hashSeed = state.hashSeed;
    LOG_INFO("[HOPSCALE] Restored: count=%u samp=1/%u filt=1/%u holdRollsRemaining=%u", count, samplingDenominator,
             filteringDenominator, state.filterDenomHoldRollsRemaining);
#endif
}

// ---------------------------------------------------------------------------
// Core API
// ---------------------------------------------------------------------------

void HopScalingModule::samplePacketForHistogram(uint32_t nodeId, uint8_t hopCount)
{
    const uint16_t hash = hashNodeId(nodeId);

    if (!passesFilter(hash, samplingDenominator))
        return;

    hopCount = std::min(hopCount, MAX_HOP);

    // Update an existing entry
    Record *entry = nullptr;
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].nodeHash == hash) {
            entry = &entries[i];
            break;
        }
    }
    if (entry) {
        entry->hops_away = hopCount;
        markCurrentHour(*entry);
        return;
    }

    // New node: trim if necessary before allocating a slot
    if (getFillPercentage() >= FILL_HIGH_PCT) {
        trimIfNeeded();
    }

    if (count < CAPACITY) {
        entries[count].nodeHash = hash;
        entries[count].hops_away = hopCount;
        entries[count].seenHoursAgo = 1u; // mark current hour
        count++;
    } else {
        LOG_WARN("[HOPSCALE] Histogram full at samp=1/%u (DENOM_MAX=%u); dropping node hash=0x%04x; hop recommendation may be "
                 "skewed!!!",
                 samplingDenominator, DENOM_MAX, hash);
    }
}

void HopScalingModule::rollHour()
{
    // Advance denominatorHistory before the tally so each slot h holds the filteringDenominator
    // that was active when seenHoursAgo bit h was set.  hourlyRaw[h] is then gated per-slot by
    // denominatorHistory[h], giving a correct population estimate for each historical hour even
    // when filteringDenominator changes between rolls.  Scale-up backfills the entire array so
    // the invariant holds retroactively (see trimIfNeeded()).
    for (uint8_t h = 12; h > 0; h--)
        denominatorHistory[h] = denominatorHistory[h - 1];
    denominatorHistory[0] = filteringDenominator;

    // 1. Tally per-hop counts and per-slot hourly activity in one pass.
    //    hourlyRaw[h]: gated per-slot by denominatorHistory[h] so the raw count and its
    //      multiplier are always consistent, even across filteringDenominator transitions.
    //    counts.*: gated uniformly by the current filteringDenominator for a consistent
    //      population estimate used by the hop-walk recommendation (step 2).
    PerHopCounts counts{};
    uint16_t hourlyRaw[13] = {};
    uint16_t trendNewThisHour = 0;
    uint16_t trendReturning = 0;
    uint16_t trendLapsed = 0;
    uint16_t trendOlderThan4h = 0;
    uint16_t trendAgingOut = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint16_t hash = entries[i].nodeHash;
        const uint32_t seen = entries[i].seenHoursAgo;

        // Per-slot hourly activity: gate each slot by its own denominator.
        for (uint8_t h = 0; h < 13; h++) {
            if ((seen & (1u << h)) && passesFilter(hash, denominatorHistory[h]))
                hourlyRaw[h]++;
        }

        // Hop counts and trend stats: uniform current-denominator gate.
        if (!passesFilter(hash, filteringDenominator))
            continue;

        if (seenInLast13h(entries[i])) {
            counts.perHop[entries[i].hops_away]++;
            counts.total++;
        }
        const bool heardThisHour = (seen & 1u) != 0u;
        const bool heardLastHour = (seen & 2u) != 0u;
        const bool hasOlderHistory = (seen >> 1u) != 0u;
        const bool recentlySilent = (seen & 0xFu) == 0u;
        if (heardThisHour && !hasOlderHistory)
            trendNewThisHour++;
        else if (heardThisHour && hasOlderHistory)
            trendReturning++;
        if (!heardThisHour && heardLastHour)
            trendLapsed++;
        if (recentlySilent && (seen & 0x1FF0u) != 0u)
            trendOlderThan4h++;
        if (seen == (1u << 12u))
            trendAgingOut++;
    }
    lastPerHopCounts = counts;

    // 1b. Compute politeness factor from the 0-2 h vs 1-3 h activity ratio.
    {
        const uint32_t recent = static_cast<uint32_t>(hourlyRaw[0]) + hourlyRaw[1];
        const uint32_t older = static_cast<uint32_t>(hourlyRaw[1]) + hourlyRaw[2];
        float activityWeight = 1.0f;
        if (older > 1 && recent > 1) {
            activityWeight = static_cast<float>(recent) / static_cast<float>(older);
        }
        if (activityWeight < ACTIVITY_WEIGHT_GENEROUS_MAX) {
            lastPoliteness = POLITENESS_GENEROUS;
        } else if (activityWeight > ACTIVITY_WEIGHT_STRICT_MIN) {
            lastPoliteness = POLITENESS_STRICT;
        } else {
            lastPoliteness = POLITENESS_DEFAULT;
        }
    }

    // 1c. Scale and cache trend stats (denominatorHistory already advanced above).
    {
        MeshTrendStats t{};
        for (uint8_t h = 0; h < 13; h++) {
            const uint32_t s = static_cast<uint32_t>(hourlyRaw[h]) * denominatorHistory[h];
            t.scaledPerHour[h] = static_cast<uint16_t>(std::min<uint32_t>(s, UINT16_MAX));
        }
        auto scale = [&](uint16_t raw) -> uint16_t {
            return static_cast<uint16_t>(std::min<uint32_t>(static_cast<uint32_t>(raw) * filteringDenominator, UINT16_MAX));
        };
        t.newThisHour = scale(trendNewThisHour);
        t.returningThisHour = scale(trendReturning);
        t.lapsedSinceLastHour = scale(trendLapsed);
        t.olderThan4h = scale(trendOlderThan4h);
        t.agingOut = scale(trendAgingOut);
        lastTrendStats = t;
    }

    // 2. Walk scaled hop buckets to produce a hop-limit recommendation.
    uint8_t suggested = MAX_HOP;
    if (counts.total > 0) {
        uint32_t cumulative = 0;
        for (uint8_t hop = 0; hop <= MAX_HOP; hop++) {
            cumulative += static_cast<uint32_t>(counts.perHop[hop]) * filteringDenominator;
            if (cumulative >= TARGET_AFFECTED_NODES) {
                suggested = hop;
                break;
            }
        }
        if (suggested < MAX_HOP) {
            const uint32_t atNext = static_cast<uint32_t>(counts.perHop[suggested + 1]) * filteringDenominator;
            const float politeLimit = static_cast<float>(TARGET_AFFECTED_NODES) * lastPoliteness;
            if (static_cast<float>(cumulative + atNext) <= politeLimit) {
                suggested++;
            }
        }
    }
    lastSuggestedHop = suggested;

    // 3. Log scaled per-hop counts and recommendation.
    {
        uint16_t scaled[MAX_HOP + 1];
        for (uint8_t h = 0; h <= MAX_HOP; h++) {
            const uint32_t s = static_cast<uint32_t>(counts.perHop[h]) * filteringDenominator;
            scaled[h] = static_cast<uint16_t>(std::min<uint32_t>(s, UINT16_MAX));
        }
        const uint32_t scaledTotal = static_cast<uint32_t>(counts.total) * filteringDenominator;
        memcpy(lastScaledPerHop, scaled, sizeof(lastScaledPerHop));
        LOG_INFO("[HOPSCALE] rollHour: entries=%u/128 samp=1/%u filt=1/%u counted=%u est=%u suggestedHop=%u polite=%.2f", count,
                 samplingDenominator, filteringDenominator, counts.total, static_cast<unsigned>(scaledTotal), suggested,
                 static_cast<double>(lastPoliteness));

        const auto &ts = lastTrendStats;
        LOG_INFO("[HOPSCALE] scaledSeenPerHour (h0=now): [%u %u %u %u %u %u %u %u %u %u %u %u %u]", ts.scaledPerHour[0],
                 ts.scaledPerHour[1], ts.scaledPerHour[2], ts.scaledPerHour[3], ts.scaledPerHour[4], ts.scaledPerHour[5],
                 ts.scaledPerHour[6], ts.scaledPerHour[7], ts.scaledPerHour[8], ts.scaledPerHour[9], ts.scaledPerHour[10],
                 ts.scaledPerHour[11], ts.scaledPerHour[12]);
        LOG_INFO("[HOPSCALE] trend: new=%u returning=%u lapsed=%u olderThan4h=%u agingOut=%u", ts.newThisHour,
                 ts.returningThisHour, ts.lapsedSinceLastHour, ts.olderThan4h, ts.agingOut);
    }

    // 4. Scale-down check: if fewer than FILL_LOW_PCT% of capacity pass the filteringDenominator
    //    gate and are active, halve samplingDenominator to admit more nodes.
    //    Note: during a filteringDenominator hold period, lowering samplingDenominator does not
    //    immediately improve counts.total (new admissions don't pass the elevated
    //    filteringDenominator).  On a genuinely quieting mesh this check can therefore fire on
    //    consecutive hours, cascading samplingDenominator toward DENOM_MIN.  This is intentional:
    //    rapid re-admission allows quick recovery if the mesh returns.  The hop recommendation
    //    stays conservative (MAX_HOP) throughout because filteringDenominator remains elevated;
    //    step 5 below re-synchronises the denominators once the hold expires.
    if (counts.total * 100u < static_cast<uint32_t>(CAPACITY) * FILL_LOW_PCT) {
        if (samplingDenominator > DENOM_MIN) {
            samplingDenominator = static_cast<uint8_t>(samplingDenominator / 2u);
            LOG_INFO("[HOPSCALE] Scale-down: sampling denom halved to %u (filter denom=%u)", samplingDenominator,
                     filteringDenominator);
        }
    }

    // 5. Tick down the hold counter; once it reaches zero, halve filteringDenominator toward
    //    samplingDenominator once per rollHour() (= once per hour) rather than a single jump:
    //    avoids a sudden large change in the hop-walk count when samplingDenominator cascaded
    //    down significantly during the hold period.  No new hold is placed on each step — the
    //    13-roll hold already guaranteed that re-admitted nodes have full seenHoursAgo history;
    //    further pacing is provided naturally by the 1-step-per-hour rate.  denominatorHistory
    //    is updated automatically by the shift at the top of rollHour(), so no backfill here.
    if (filteringDenominator > samplingDenominator) {
        if (filteringDenomHoldRollsRemaining > 0)
            filteringDenomHoldRollsRemaining--;
        if (filteringDenomHoldRollsRemaining == 0) {
            const uint8_t stepped = static_cast<uint8_t>(filteringDenominator / 2u);
            filteringDenominator = (stepped > samplingDenominator) ? stepped : samplingDenominator;
            LOG_INFO("[HOPSCALE] Filter denom stepped to %u (samp=1/%u)", filteringDenominator, samplingDenominator);
        }
    }

    // 6. Shift all seen bitmaps left by one slot (opens a fresh slot for the new hour).
    for (uint8_t i = 0; i < count; i++) {
        rollSeenBits(entries[i]);
    }

    if (histogramRollCount < 255)
        histogramRollCount++;

    saveToDisk();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void HopScalingModule::trimIfNeeded()
{
    // Step 1: evict stale entries (not seen in any of the past 13 hours).
    uint8_t newCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (seenInLast13h(entries[i])) {
            if (i != newCount) {
                entries[newCount] = entries[i];
            }
            newCount++;
        }
    }
    count = newCount;

    // Step 2: if still too full, double the sampling denominator and remove non-matching entries.
    if (getFillPercentage() >= FILL_HIGH_PCT && samplingDenominator < DENOM_MAX) {
        samplingDenominator = static_cast<uint8_t>(
            std::min<uint16_t>(static_cast<uint16_t>(samplingDenominator) * 2u, static_cast<uint16_t>(DENOM_MAX)));
        filteringDenominator = std::max(filteringDenominator, samplingDenominator);
        filteringDenomHoldRollsRemaining = FILTER_DENOM_HOLD_ROLLS;
        // Raise any denominatorHistory slot that is below the new filteringDenominator.
        // Slots already above it (recorded during a prior scale-up that hasn't fully stepped
        // down yet) are left untouched: eviction at samplingDenominator retains exactly those
        // entries, so the old higher gate remains accurate for those historical hours.
        // Slots below the new value must be raised because the eviction removed entries that
        // had been admitted at the looser old gate — the remaining entries represent a 1/N
        // subsample where N is the new filteringDenominator, not the old smaller value.
        for (uint8_t h = 0; h < 13; h++)
            denominatorHistory[h] = std::max(denominatorHistory[h], filteringDenominator);
        LOG_INFO("[HOPSCALE] Scale-up: samp denom doubled to %u (filt=%u)", samplingDenominator, filteringDenominator);

        newCount = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (passesFilter(entries[i].nodeHash, samplingDenominator)) {
                if (i != newCount) {
                    entries[newCount] = entries[i];
                }
                newCount++;
            }
        }
        count = newCount;
    }
}

void HopScalingModule::logStatusReport(bool didHourlyUpdate) const
{
    const bool histActive = (histogramRollCount > 0 && count > 0);
    const auto &histCounts = lastPerHopCounts;
    const uint8_t runsRemaining = didHourlyUpdate ? RUNS_PER_HOUR : (RUNS_PER_HOUR - runsSinceLastHourlyUpdate);
    const uint8_t minsUntilRollover = runsRemaining * (RUN_INTERVAL_MS / (60 * 1000UL));

    LOG_INFO("[HOPSCALE] hop=%u histActive=%u fill=%u%% samp=1/%u filt=1/%u entries=%u lastCounted=%u polite=%.2f "
             "nextRoll=%umin",
             lastRequiredHop, histActive ? 1u : 0u, getFillPercentage(), samplingDenominator, filteringDenominator, count,
             histCounts.total, static_cast<double>(lastPoliteness), minsUntilRollover);

    LOG_INFO("[HOPSCALE] nodes perHop: [%u %u %u %u %u %u %u %u]", histCounts.perHop[0], histCounts.perHop[1],
             histCounts.perHop[2], histCounts.perHop[3], histCounts.perHop[4], histCounts.perHop[5], histCounts.perHop[6],
             histCounts.perHop[7]);
    LOG_INFO("[HOPSCALE] last scaled perHop: [%u %u %u %u %u %u %u %u]", lastScaledPerHop[0], lastScaledPerHop[1],
             lastScaledPerHop[2], lastScaledPerHop[3], lastScaledPerHop[4], lastScaledPerHop[5], lastScaledPerHop[6],
             lastScaledPerHop[7]);
}

int32_t HopScalingModule::runOnce()
{
    const bool isFirstRun = !hasCompletedInitialRun;
    bool didHourlyUpdate = false;

    if (isFirstRun) {
        hasCompletedInitialRun = true;
        runsSinceLastHourlyUpdate = 0;
        didHourlyUpdate = true;
    } else {
        runsSinceLastHourlyUpdate++;
        if (runsSinceLastHourlyUpdate >= RUNS_PER_HOUR) {
            runsSinceLastHourlyUpdate = 0;
            didHourlyUpdate = true;
        }
    }

    if (didHourlyUpdate && !isFirstRun) {
        rollHour();
    }

    if (didHourlyUpdate) {
        lastRequiredHop = (histogramRollCount > 0 && count > 0) ? lastSuggestedHop : HOP_MAX;
    }

    logStatusReport(didHourlyUpdate);

    return RUN_INTERVAL_MS;
}

#endif
