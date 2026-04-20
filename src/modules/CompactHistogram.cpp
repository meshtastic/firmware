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
}

// ---------------------------------------------------------------------------
// Core API
// ---------------------------------------------------------------------------

void CompactHistogram::sampleRxPacket(uint32_t nodeId, uint8_t hopCount)
{
    // Reject nodes that do not pass the current sampling denominator
    if (!passesFilter(nodeId, samplingDenominator)) {
        return;
    }

    // Clamp hop count to the representable range
    hopCount = std::min(hopCount, MAX_HOP);

    // Update an existing entry
    HistogramEntry *entry = findEntry(nodeId);
    if (entry) {
        // Keep the minimum observed hop count
        if (hopCount < hopBits(entry->bitfield)) {
            entry->bitfield = packBf(seenBits(entry->bitfield), hopCount);
        }
        markCurrentHour(entry->bitfield);
        return;
    }

    // New node: trim if necessary before allocating a slot
    if (getFillPercentage() >= FILL_HIGH_PCT) {
        trimIfNeeded();
    }

    if (count < CAPACITY) {
        entries[count].nodeId = nodeId;
        entries[count].bitfield = packBf(0u, hopCount);
        markCurrentHour(entries[count].bitfield);
        count++;
    }
    // If still full after trimming, silently drop the new entry
}

uint8_t CompactHistogram::rollHour()
{
    // 1. Tally per-hop counts for entries that pass the filtering denominator
    //    AND have been seen in at least one of the past 12 hours.
    PerHopCounts counts{};
    for (uint8_t i = 0; i < count; i++) {
        if (passesFilter(entries[i].nodeId, filteringDenominator) && seenInLast12h(entries[i].bitfield)) {
            const uint8_t hop = hopBits(entries[i].bitfield);
            counts.perHop[hop]++;
            counts.total++;
        }
    }
    lastPerHopCounts = counts;

    // 2. Scale-down check: if fewer than FILL_LOW_PCT% of capacity are active (pass
    //    filteringDenominator AND seen in the last 12 h), halve samplingDenominator.
    if (counts.total * 100u < static_cast<uint32_t>(CAPACITY) * FILL_LOW_PCT) {
        if (samplingDenominator > DENOM_MIN) {
            samplingDenominator = static_cast<uint8_t>(samplingDenominator / 2u);
            LOG_INFO("[HISTOGRAM] Scale-down: sampling denom halved to %u (filter denom=%u)", samplingDenominator,
                     filteringDenominator);
        }
    }

    // 3. Drop filteringDenominator back to samplingDenominator once the 12-h hold has expired.
    if (filteringDenominator > samplingDenominator) {
        if ((nowMs() - filteringDenomElevatedAt) >= FILTER_DENOM_HOLD_MS) {
            filteringDenominator = samplingDenominator;
            LOG_INFO("[HISTOGRAM] Filter denom dropped to %u after 12-h hold", filteringDenominator);
        }
    }

    // 4. Shift all seen bitmaps left by one slot (opens a fresh slot for the new hour).
    for (uint8_t i = 0; i < count; i++) {
        entries[i].bitfield = rollSeenBits(entries[i].bitfield);
    }

    // 5. Walk scaled hop buckets to produce a hop-limit recommendation.
    const uint8_t suggested = walkHopBuckets(counts);
    lastSuggestedHop = suggested;

    // 6. Log scaled per-hop counts and recommendation.
    uint16_t scaled[MAX_HOP + 1];
    for (uint8_t h = 0; h <= MAX_HOP; h++) {
        const uint32_t s = static_cast<uint32_t>(counts.perHop[h]) * filteringDenominator;
        scaled[h] = static_cast<uint16_t>(std::min<uint32_t>(s, UINT16_MAX));
    }
    LOG_INFO("[HISTOGRAM] rollHour: entries=%u samp=1/%u filt=1/%u counted=%u suggestedHop=%u", count, samplingDenominator,
             filteringDenominator, counts.total, suggested);
    LOG_INFO("[HISTOGRAM] scaled perHop: [%u %u %u %u %u %u %u %u]", scaled[0], scaled[1], scaled[2], scaled[3], scaled[4],
             scaled[5], scaled[6], scaled[7]);

    return suggested;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void CompactHistogram::trimIfNeeded()
{
    // Step 1: evict stale entries (not seen in any of the past 12 hours).
    uint8_t newCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (seenInLast12h(entries[i].bitfield)) {
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
            if (passesFilter(entries[i].nodeId, samplingDenominator)) {
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
    // in at least one of the past 12 hours.  Stale entries that merely have a
    // matching nodeId should not keep the denominator elevated.
    uint8_t n = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (passesFilter(entries[i].nodeId, filteringDenominator) && seenInLast12h(entries[i].bitfield)) {
            n++;
        }
    }
    return n;
}

HistogramEntry *CompactHistogram::findEntry(uint32_t nodeId)
{
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].nodeId == nodeId) {
            return &entries[i];
        }
    }
    return nullptr;
}

uint8_t CompactHistogram::walkHopBuckets(const PerHopCounts &counts) const
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
    // POLITENESS_FACTOR × TARGET_AFFECTED_NODES.  The next hop's sampled count may be
    // zero due to sparse sampling even when real nodes exist there, so we do not gate on it.
    if (baseHop < MAX_HOP) {
        const uint32_t atNext = static_cast<uint32_t>(counts.perHop[baseHop + 1]) * filteringDenominator;
        const float politeLimit = static_cast<float>(TARGET_AFFECTED_NODES) * POLITENESS_FACTOR;
        if (static_cast<float>(cumulative + atNext) <= politeLimit) {
            baseHop++;
        }
    }

    return baseHop;
}
