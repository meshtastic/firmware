#pragma once

#include "configuration.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

/**
 * CompactHistogram: Sampled node-tracking histogram for mesh hop-distance estimation.
 *
 * Memory layout: 512 bytes total (128 entries × 4 bytes/entry, no padding)
 *   - 16-bit XOR-fold hash of node ID
 *   - 3-bit hops away (0–7)
 *   - 13-bit hourly seen bitmap
 *   All three fields are packed into a single 32-bit Record; sizeof(Record) == 4.
 *
 * Sampling:
 *   - A node is added only when (nodeId & (samplingDenominator – 1)) == 0
 *   - samplingDenominator starts at 1 (sample all), doubles when the list exceeds FILL_HIGH_PCT
 *   - filteringDenominator tracks samplingDenominator upward immediately but does not drop back
 *     down until FILTER_DENOM_HOLD_MS (13 h) have elapsed since the last scale-up
 *
 * Hourly rollover (rollHour()):
 *   - Summarises per-hop node counts for entries matching filteringDenominator and seen in the
 *     last 13 hours
 *   - Scales each hop bucket by filteringDenominator and walks the buckets to recommend a hop
 *     limit, matching the same algorithm used in HopScalingModule
 *   - Shifts the 13-bit seen bitmap left by one slot to open a fresh slot for the new hour;
 *     nodes not seen in 13 consecutive hours have all seen bits cleared (stale)
 *   - Checks for scale-down: if fewer than FILL_LOW_PCT of capacity pass filteringDenominator,
 *     samplingDenominator is halved (filteringDenominator is held until the 13-h lock expires)
 */

struct Record {
    uint32_t nodeHash : 16;     // XOR-fold hash of full 32-bit node ID (see CompactHistogram::hashNodeId)
    uint32_t hops_away : 3;     // hop distance (0–7)
    uint32_t seenHoursAgo : 13; // per-hour seen bitmap (bit 0 = current hour, bit 12 = 12 h ago)
};
static_assert(sizeof(Record) == 4);

class CompactHistogram
{
  public:
    // -----------------------------------------------------------------------
    // Capacity and memory layout
    // -----------------------------------------------------------------------
    static constexpr size_t CAPACITY = 128;
    static constexpr size_t ENTRY_BYTES = sizeof(Record);         // 4 bytes per entry (32-bit packed struct)
    static constexpr size_t TOTAL_BYTES = CAPACITY * ENTRY_BYTES; // 512 bytes total

    // -----------------------------------------------------------------------
    // Denominator limits (must be powers of 2)
    // -----------------------------------------------------------------------
    static constexpr uint8_t DENOM_MIN = 1;
    static constexpr uint8_t DENOM_MAX = 128;

    // Maximum representable hop value (3 bits → 0–7)
    static constexpr uint8_t MAX_HOP = 7;

    // Fill-level thresholds (percent of CAPACITY)
    static constexpr uint8_t FILL_HIGH_PCT = 80; // Trigger trim / scale-up above this
    static constexpr uint8_t FILL_LOW_PCT = 20;  // Trigger scale-down below this

    // How long filteringDenominator is held at an elevated level before it may drop
    static constexpr uint32_t FILTER_DENOM_HOLD_MS = 13UL * 60UL * 60UL * 1000UL; // 13 h

    // Hop-walk: target cumulative affected-node count when choosing a hop limit
    static constexpr uint16_t TARGET_AFFECTED_NODES = 40;

    // Politeness factors for the one-hop extension check in the hop walk
    static constexpr float POLITENESS_GENEROUS = 2.0f; // Quiet mesh: allow doubling
    static constexpr float POLITENESS_DEFAULT = 1.5f;  // Stable mesh: allow 50% growth
    static constexpr float POLITENESS_STRICT = 1.25f;  // Busy mesh: allow 25% growth

    // Activity weight thresholds (ratio of 0-2 h window vs 1-3 h window)
    static constexpr float ACTIVITY_WEIGHT_GENEROUS_MAX = 0.9f; // Below this: GENEROUS
    static constexpr float ACTIVITY_WEIGHT_STRICT_MIN = 1.2f;   // Above this: STRICT

    // -----------------------------------------------------------------------
    // Types
    // -----------------------------------------------------------------------

    /// Per-hop node counts produced at each hourly rollover.
    struct PerHopCounts {
        uint16_t perHop[MAX_HOP + 1] = {};
        uint16_t total = 0;
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    CompactHistogram();
    ~CompactHistogram() = default;

    // -----------------------------------------------------------------------
    // Core API
    // -----------------------------------------------------------------------

    /// Record a received packet.
    /// Adds or updates an entry when (nodeId & (samplingDenominator – 1)) == 0.
    /// Marks the current hour as seen and updates the stored hop count to the last observed value.
    /// Triggers a trim pass if the list exceeds FILL_HIGH_PCT after the insertion.
    void sampleRxPacket(uint32_t nodeId, uint8_t hopCount);

    /// Perform hourly rollover.
    /// 1. Tallies per-hop counts for entries matching filteringDenominator and seen in 13 h.
    /// 2. Checks for scale-down (< FILL_LOW_PCT of capacity pass filteringDenominator).
    /// 3. Drops filteringDenominator to samplingDenominator if the 13-h hold has expired.
    /// 4. Shifts all seen bitmaps left by one hour slot.
    /// 5. Walks the scaled hop buckets and returns the recommended hop limit.
    uint8_t rollHour();

    /// Reset all entries and state.
    void clear();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    uint8_t getEntryCount() const { return count; }
    uint8_t getFillPercentage() const { return static_cast<uint8_t>((static_cast<uint16_t>(count) * 100u) / CAPACITY); }
    uint8_t getSamplingDenominator() const { return samplingDenominator; }
    uint8_t getFilteringDenominator() const { return filteringDenominator; }
    float getPoliteness() const { return lastPoliteness; }

    /// Force both sampling and filtering denominators to a specific value.
    /// Intended for unit tests that need a deterministic starting denominator.
    void setSamplingDenominator(uint8_t d)
    {
        samplingDenominator = (d < DENOM_MIN) ? DENOM_MIN : (d > DENOM_MAX ? DENOM_MAX : d);
        filteringDenominator = samplingDenominator;
        filteringDenomElevatedAt = 0;
    }
    const PerHopCounts &getLastPerHopCounts() const { return lastPerHopCounts; }
    uint8_t getLastSuggestedHop() const { return lastSuggestedHop; }

#ifdef UNIT_TEST
    // Writable from the test file as CompactHistogram::s_testNowMs; drives nowMs() in UNIT_TEST builds.
    inline static uint32_t s_testNowMs = 0;
    /// Override the per-session hash seed. Use in tests that need a specific sampling distribution.
    void setHashSeed(uint16_t seed) { hashSeed = seed; }
    uint16_t getHashSeed() const { return hashSeed; }
    /// Expose hashNodeId for tests that need to compute which node IDs pass a given denominator.
    uint16_t hashNodeIdPublic(uint32_t nodeId) const { return hashNodeId(nodeId); }
#endif

  private:
    // -----------------------------------------------------------------------
    // Storage
    // -----------------------------------------------------------------------
    Record entries[CAPACITY] = {};
    uint8_t count = 0;

    // -----------------------------------------------------------------------
    // Denominator state
    // -----------------------------------------------------------------------
    uint8_t samplingDenominator = DENOM_MIN;  // Current ingest filter (can go up and down)
    uint8_t filteringDenominator = DENOM_MIN; // Current count filter (held high for 13 h)
    uint32_t filteringDenomElevatedAt = 0;    // nowMs() when filteringDenominator was last raised
    uint16_t hashSeed = 0;                    // Per-session seed XORed into hashNodeId(); randomised on clear()

    // -----------------------------------------------------------------------
    // Cached hourly results
    // -----------------------------------------------------------------------
    PerHopCounts lastPerHopCounts = {};
    uint8_t lastSuggestedHop = MAX_HOP;
    float lastPoliteness = POLITENESS_DEFAULT;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Remove stale entries (seen-bits all zero) and, if the list is still crowded,
    /// double samplingDenominator and filteringDenominator and remove non-matching entries.
    void trimIfNeeded();

    /// Count entries whose nodeId passes (nodeId & (filteringDenominator-1)) == 0.
    uint8_t countPassingFilter() const;

    /// Linear scan for an existing entry with this nodeId. Returns nullptr if absent.
    Record *findEntry(uint16_t nodeHash);

    /// Walk hop buckets scaled by filteringDenominator; return recommended hop limit.
    uint8_t walkHopBuckets(const PerHopCounts &counts, float politeness) const;

    // -----------------------------------------------------------------------
    // Record helpers (all inline, no heap)
    // -----------------------------------------------------------------------

    // Record field semantics:
    //   nodeHash     → XOR-fold of full 32-bit node ID to 16 bits
    //   hops_away    → hop distance (0–7)
    //   seenHoursAgo → 13-bit per-hour seen bitmap
    //                  bit 0  = seen in the current / most-recent hour
    //                  bit 12 = seen 12 hours ago
    //                  Shifts left on each rollHour(); 0 means not seen in 13 h.

    /// XOR-fold + golden-ratio hash of a 32-bit node ID to 16 bits, mixed with the session seed.
    /// Multiplying by floor(2^32 / φ) gives uniform avalanche; XORing the seed ensures different
    /// devices (or the same device after a clear()) sample a different subset of node IDs.
    /// For seed=0 the function is deterministic, which is used in UNIT_TEST builds.
    uint16_t hashNodeId(uint32_t nodeId) const { return static_cast<uint16_t>((nodeId * 2654435761u) >> 16) ^ hashSeed; }

    static bool seenInLast13h(const Record &r) { return r.seenHoursAgo != 0u; }
    static void markCurrentHour(Record &r) { r.seenHoursAgo |= 1u; }
    static void rollSeenBits(Record &r) { r.seenHoursAgo = (r.seenHoursAgo << 1u) & 0x1FFFu; }

    /// True when (nodeHash & (denom-1)) == 0.  Always true when denom == 1.
    static bool passesFilter(uint16_t nodeHash, uint8_t denom) { return (nodeHash & static_cast<uint16_t>(denom - 1u)) == 0u; }

  public:
    // -----------------------------------------------------------------------
    // Clock — public so HopScalingModule can share the same timebase
    // -----------------------------------------------------------------------
#ifdef UNIT_TEST
    static uint32_t nowMs() { return s_testNowMs; }
#else
    static uint32_t nowMs() { return millis(); }
#endif
};
