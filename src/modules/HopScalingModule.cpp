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
constexpr uint32_t HISTOGRAM_STATE_MAGIC = 0x48535432; // 'HST2' — layout v2
constexpr uint8_t HISTOGRAM_STATE_VERSION = 1;
constexpr const char *HISTOGRAM_STATE_FILE = "/prefs/hopScalingState.bin";

#pragma pack(push, 1)
struct PersistedHistogram {
    uint32_t magic;
    uint8_t version;
    uint8_t samplingDenominator;
    uint8_t filteringDenominator;
    uint32_t filterDenomRemainMs; // remaining hold duration; 0 when not elevated
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
    filteringDenomElevatedAt = 0;
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
        // Persist remaining hold time rather than the absolute timestamp so the value is
        // still meaningful after a reboot (nowMs() resets to 0 on restart).
        if (filteringDenominator > samplingDenominator) {
            const uint32_t elapsed = nowMs() - filteringDenomElevatedAt;
            state.filterDenomRemainMs = (elapsed < FILTER_DENOM_HOLD_MS) ? (FILTER_DENOM_HOLD_MS - elapsed) : 0u;
        }
        state.hashSeed = hashSeed;
        // Save all CAPACITY slots; count is reconstructed on load by scanning seenHoursAgo.
        memcpy(state.entries, entries, sizeof(state.entries));
        file.write(reinterpret_cast<const uint8_t *>(&state), sizeof(state));
        file.flush();
        file.close();
        LOG_DEBUG("[HOPSCALE] Saved: count=%u samp=1/%u filt=1/%u holdRemainMs=%u", count, samplingDenominator,
                  filteringDenominator, state.filterDenomRemainMs);
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
    // Back-date the elevation timestamp so the remaining hold duration is honoured.
    if (state.filterDenomRemainMs > 0 && filteringDenominator > samplingDenominator) {
        filteringDenomElevatedAt = nowMs() - (FILTER_DENOM_HOLD_MS - state.filterDenomRemainMs);
    } else {
        filteringDenomElevatedAt = 0;
    }
    // denominatorHistory can't be recovered; initialise all slots to filteringDenominator so
    // the first few post-reboot scaledPerHour values use a safe (slightly conservative) multiplier.
    memset(denominatorHistory, filteringDenominator, sizeof(denominatorHistory));
    hashSeed = state.hashSeed;
    LOG_INFO("[HOPSCALE] Restored: count=%u samp=1/%u filt=1/%u holdRemainMs=%u", count, samplingDenominator,
             filteringDenominator, state.filterDenomRemainMs);
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
    }
    // If still full after trimming, silently drop the new entry
}

void HopScalingModule::rollHour()
{
    // 1. Tally per-hop counts for entries that pass the filtering denominator
    //    AND have been seen in at least one of the past 13 hours.
    //    Also tally per-hour seen counts across all 13 seen-bit slots (before the bitmap shift),
    //    and derive mesh trend counters from the same pass.
    PerHopCounts counts{};
    uint16_t hourlyRaw[13] = {};
    uint16_t trendNewThisHour = 0;
    uint16_t trendReturning = 0;
    uint16_t trendLapsed = 0;
    uint16_t trendOlderThan4h = 0;
    uint16_t trendAgingOut = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (!passesFilter(entries[i].nodeHash, filteringDenominator))
            continue;
        const uint32_t seen = entries[i].seenHoursAgo;
        for (uint8_t h = 0; h < 13; h++) {
            if (seen & (1u << h))
                hourlyRaw[h]++;
        }
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

    // 1c. Scale and cache trend stats using per-hour denominator history.
    {
        for (uint8_t h = 12; h > 0; h--)
            denominatorHistory[h] = denominatorHistory[h - 1];
        denominatorHistory[0] = filteringDenominator;

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

    // 4. Scale-down check: if fewer than FILL_LOW_PCT% of capacity are active, halve samplingDenominator.
    if (counts.total * 100u < static_cast<uint32_t>(CAPACITY) * FILL_LOW_PCT) {
        if (samplingDenominator > DENOM_MIN) {
            samplingDenominator = static_cast<uint8_t>(samplingDenominator / 2u);
            LOG_INFO("[HOPSCALE] Scale-down: sampling denom halved to %u (filter denom=%u)", samplingDenominator,
                     filteringDenominator);
        }
    }

    // 5. Drop filteringDenominator back to samplingDenominator once the 13-h hold has expired.
    if (filteringDenominator > samplingDenominator) {
        if ((nowMs() - filteringDenomElevatedAt) >= FILTER_DENOM_HOLD_MS) {
            filteringDenominator = samplingDenominator;
            LOG_INFO("[HOPSCALE] Filter denom dropped to %u after 13-h hold", filteringDenominator);
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
        filteringDenominator = samplingDenominator;
        filteringDenomElevatedAt = nowMs();
        LOG_INFO("[HOPSCALE] Scale-up: denom doubled to %u", samplingDenominator);

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

    LOG_INFO("[HOPSCALE] hop=%u hourly=%u histActive=%u fill=%u%% samp=1/%u filt=1/%u entries=%u lastCounted=%u polite=%.2f "
             "nextRoll=%umin",
             lastRequiredHop, didHourlyUpdate ? 1u : 0u, histActive ? 1u : 0u, getFillPercentage(), samplingDenominator,
             filteringDenominator, count, histCounts.total, static_cast<double>(lastPoliteness), minsUntilRollover);

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
