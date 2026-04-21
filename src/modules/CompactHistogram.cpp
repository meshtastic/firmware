#include "CompactHistogram.h"
#include "configuration.h"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CompactHistogram::CompactHistogram()
{
    clear();
}

void CompactHistogram::clear()
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
    // Pick a fresh random seed so each session (or post-clear state) samples a different
    // subset of node IDs, preventing systematic bias toward/against specific address ranges.
    hashSeed = static_cast<uint16_t>(random());
#else
    hashSeed = 0; // deterministic in unit tests
#endif
}

// ---------------------------------------------------------------------------
// Core API
// ---------------------------------------------------------------------------

void CompactHistogram::sampleRxPacket(uint32_t nodeId, uint8_t hopCount)
{
    // Hash first; all sampling and storage decisions use the 16-bit hash.
    const uint16_t hash = hashNodeId(nodeId);

    // Reject nodes that do not pass the current sampling denominator
    if (!passesFilter(hash, samplingDenominator)) {
        return;
    }

    // Clamp hop count to the representable range
    hopCount = std::min(hopCount, MAX_HOP);

    // Update an existing entry
    Record *entry = findEntry(hash);
    if (entry) {
        // Update to the most-recently observed hop count
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

uint8_t CompactHistogram::rollHour()
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
        // Trend classification (bit 0 = this hour, bit 12 = 12 h ago, before the shift).
        const bool heardThisHour = (seen & 1u) != 0u;
        const bool heardLastHour = (seen & 2u) != 0u;
        const bool hasOlderHistory = (seen >> 1u) != 0u; // any bit 1-12 set
        const bool recentlySilent = (seen & 0xFu) == 0u; // bits 0-3 all clear
        if (heardThisHour && !hasOlderHistory)
            trendNewThisHour++;
        else if (heardThisHour && hasOlderHistory)
            trendReturning++;
        if (!heardThisHour && heardLastHour)
            trendLapsed++;
        if (recentlySilent && (seen & 0x1FF0u) != 0u) // bits 4-12 any set
            trendOlderThan4h++;
        if (seen == (1u << 12u)) // only the oldest slot remains
            trendAgingOut++;
    }
    lastPerHopCounts = counts;

    // 1b. Compute politeness factor from the 0-2 h vs 1-3 h activity ratio.
    //     hourlyRaw[0] = nodes heard in the just-completed hour (h=0).
    //     hourlyRaw[1] = h=1 (1-2 h ago), hourlyRaw[2] = h=2 (2-3 h ago).
    //     Overlapping windows share the h=1 bucket, smoothing boundary effects.
    {
        const uint32_t recent = static_cast<uint32_t>(hourlyRaw[0]) + hourlyRaw[1]; // 0-2 h
        const uint32_t older = static_cast<uint32_t>(hourlyRaw[1]) + hourlyRaw[2];  // 1-3 h
        float activityWeight = 1.0f;                                                // neutral fallback
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

    // 1c. Scale and cache trend stats.
    //     Per-hour counts use per-hour denominators: the filteringDenominator that was in
    //     effect when each hour's data was sampled.  This means a scale-down does not
    //     retroactively undercount older hour slots, and a scale-up doesn't overcount them.
    //     Shift the history ring now so denominatorHistory[h] corresponds to hourlyRaw[h].
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

    // 2. Scale-down check: if fewer than FILL_LOW_PCT% of capacity are active (pass
    //    filteringDenominator AND seen in the last 13 h), halve samplingDenominator.
    if (counts.total * 100u < static_cast<uint32_t>(CAPACITY) * FILL_LOW_PCT) {
        if (samplingDenominator > DENOM_MIN) {
            samplingDenominator = static_cast<uint8_t>(samplingDenominator / 2u);
            LOG_INFO("[HISTOGRAM] Scale-down: sampling denom halved to %u (filter denom=%u)", samplingDenominator,
                     filteringDenominator);
        }
    }

    // 3. Drop filteringDenominator back to samplingDenominator once the 13-h hold has expired.
    if (filteringDenominator > samplingDenominator) {
        if ((nowMs() - filteringDenomElevatedAt) >= FILTER_DENOM_HOLD_MS) {
            filteringDenominator = samplingDenominator;
            LOG_INFO("[HISTOGRAM] Filter denom dropped to %u after 13-h hold", filteringDenominator);
        }
    }

    // 4. Shift all seen bitmaps left by one slot (opens a fresh slot for the new hour).
    for (uint8_t i = 0; i < count; i++) {
        rollSeenBits(entries[i]);
    }

    // 5. Walk scaled hop buckets to produce a hop-limit recommendation.
    const uint8_t suggested = walkHopBuckets(counts, lastPoliteness);
    lastSuggestedHop = suggested;

    // 6. Log scaled per-hop counts and recommendation.
    uint16_t scaled[MAX_HOP + 1];
    for (uint8_t h = 0; h <= MAX_HOP; h++) {
        const uint32_t s = static_cast<uint32_t>(counts.perHop[h]) * filteringDenominator;
        scaled[h] = static_cast<uint16_t>(std::min<uint32_t>(s, UINT16_MAX));
    }
    const uint32_t scaledTotal = static_cast<uint32_t>(counts.total) * filteringDenominator;
    LOG_INFO("[HISTOGRAM] rollHour: entries=%u/128 samp=1/%u filt=1/%u counted=%u est=%u suggestedHop=%u polite=%.2f", count,
             samplingDenominator, filteringDenominator, counts.total, static_cast<unsigned>(scaledTotal), suggested,
             lastPoliteness);
    LOG_INFO("[HISTOGRAM] scaled perHop: [%u %u %u %u %u %u %u %u]", scaled[0], scaled[1], scaled[2], scaled[3], scaled[4],
             scaled[5], scaled[6], scaled[7]);

    // Per-hour node count matrix and mesh trend — use the cached lastTrendStats values.
    const auto &ts = lastTrendStats;
    LOG_INFO("[HISTOGRAM] scaledSeenPerHour (h0=now): [%u %u %u %u %u %u %u %u %u %u %u %u %u]", ts.scaledPerHour[0],
             ts.scaledPerHour[1], ts.scaledPerHour[2], ts.scaledPerHour[3], ts.scaledPerHour[4], ts.scaledPerHour[5],
             ts.scaledPerHour[6], ts.scaledPerHour[7], ts.scaledPerHour[8], ts.scaledPerHour[9], ts.scaledPerHour[10],
             ts.scaledPerHour[11], ts.scaledPerHour[12]);
    LOG_INFO("[HISTOGRAM] trend: new=%u returning=%u lapsed=%u olderThan4h=%u agingOut=%u", ts.newThisHour, ts.returningThisHour,
             ts.lapsedSinceLastHour, ts.olderThan4h, ts.agingOut);

    return suggested;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void CompactHistogram::trimIfNeeded()
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

    // Step 2: if still too full, double the sampling denominator and remove entries
    //         that no longer match the new (stricter) filter.
    if (getFillPercentage() >= FILL_HIGH_PCT && samplingDenominator < DENOM_MAX) {
        samplingDenominator = static_cast<uint8_t>(
            std::min<uint16_t>(static_cast<uint16_t>(samplingDenominator) * 2u, static_cast<uint16_t>(DENOM_MAX)));
        filteringDenominator = samplingDenominator;
        filteringDenomElevatedAt = nowMs();
        LOG_INFO("[HISTOGRAM] Scale-up: denom doubled to %u", samplingDenominator);

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

uint8_t CompactHistogram::countPassingFilter() const
{
    // Count only entries that pass the filtering denominator AND have been seen
    // in at least one of the past 13 hours.  Stale entries that merely have a
    // matching nodeId should not keep the denominator elevated.
    uint8_t n = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (passesFilter(entries[i].nodeHash, filteringDenominator) && seenInLast13h(entries[i])) {
            n++;
        }
    }
    return n;
}

Record *CompactHistogram::findEntry(uint16_t nodeHash)
{
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].nodeHash == nodeHash) {
            return &entries[i];
        }
    }
    return nullptr;
}

uint8_t CompactHistogram::walkHopBuckets(const PerHopCounts &counts, float politeness) const
{
    if (counts.total == 0) {
        return MAX_HOP;
    }

    // Scale each bucket by filteringDenominator to estimate the full mesh population per hop.
    uint32_t cumulative = 0;
    uint8_t baseHop = MAX_HOP;

    for (uint8_t hop = 0; hop <= MAX_HOP; hop++) {
        const uint32_t scaled = static_cast<uint32_t>(counts.perHop[hop]) * filteringDenominator;
        cumulative += scaled;
        if (cumulative >= TARGET_AFFECTED_NODES) {
            baseHop = hop;
            break;
        }
    }

    // Politeness extension: extend by one hop if the cumulative count stays within
    // politeness × TARGET_AFFECTED_NODES.  The next hop's sampled count may be
    // zero due to sparse sampling even when real nodes exist there, so we do not gate on it.
    if (baseHop < MAX_HOP) {
        const uint32_t atNext = static_cast<uint32_t>(counts.perHop[baseHop + 1]) * filteringDenominator;
        const float politeLimit = static_cast<float>(TARGET_AFFECTED_NODES) * politeness;
        if (static_cast<float>(cumulative + atNext) <= politeLimit) {
            baseHop++;
        }
    }

    return baseHop;
}
