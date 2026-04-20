#pragma once

#include "configuration.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

/**
 * CompactHistogram: Sampled node-tracking histogram for mesh hop-distance estimation.
 *
 * Memory layout: 1024 bytes total (128 entries × 8 bytes/entry due to alignment)
 *   - 32-bit node ID (full range, checked against bitwise sampling denominator)
 *   - 16-bit bitfield: bits[3:0] = hops away (4-bit, 0–7), bits[15:4] = hourly seen (12 bits)
 *   - 2 bytes natural compiler padding (HistogramEntry is 8 bytes total on all tested platforms)
 *
 * Sampling:
 *   - A node is added only when (nodeId & (samplingDenominator – 1)) == 0
 *   - samplingDenominator starts at 1 (sample all), doubles when the list exceeds FILL_HIGH_PCT
 *   - filteringDenominator tracks samplingDenominator upward immediately but does not drop back
 *     down until FILTER_DENOM_HOLD_MS (12 h) have elapsed since the last scale-up
 *
 * Hourly rollover (rollHour()):
 *   - Summarises per-hop node counts for entries matching filteringDenominator and seen in the
 *     last 12 hours
 *   - Scales each hop bucket by filteringDenominator and walks the buckets to recommend a hop
 *     limit, matching the same algorithm used in HopScalingModule
 *   - Shifts the 12-bit seen bitmap left by one slot to open a fresh slot for the new hour;
 *     nodes not seen in 12 consecutive hours have all seen bits cleared (stale)
 *   - Checks for scale-down: if fewer than FILL_LOW_PCT of capacity pass filteringDenominator,
 *     samplingDenominator is halved (filteringDenominator is held until the 12-h lock expires)
 */

struct HistogramEntry {
    uint32_t nodeId;   // Full 32-bit node ID
    uint16_t bitfield; // bits[3:0] = hops away (0–7), bits[15:4] = per-hour seen (12 bits)
};

class CompactHistogram
{
  public:
    // -----------------------------------------------------------------------
    // Capacity and memory layout
    // -----------------------------------------------------------------------
    static constexpr size_t CAPACITY = 128;
    static constexpr size_t ENTRY_BYTES = sizeof(HistogramEntry); // 8 bytes per entry (4+2+2pad)
    static constexpr size_t TOTAL_BYTES = CAPACITY * ENTRY_BYTES; // 1024 bytes total

    // -----------------------------------------------------------------------
    // Denominator limits (must be powers of 2)
    // -----------------------------------------------------------------------
    static constexpr uint8_t DENOM_MIN = 1;
    static constexpr uint8_t DENOM_MAX = 128;

    // Maximum representable hop value (4 bits → 0–15; meaningful range 0–7)
    static constexpr uint8_t MAX_HOP = 7;

    // Fill-level thresholds (percent of CAPACITY)
    static constexpr uint8_t FILL_HIGH_PCT = 80; // Trigger trim / scale-up above this
    static constexpr uint8_t FILL_LOW_PCT = 20;  // Trigger scale-down below this

    // How long filteringDenominator is held at an elevated level before it may drop
    static constexpr uint32_t FILTER_DENOM_HOLD_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h

    // Hop-walk: target cumulative affected-node count when choosing a hop limit
    static constexpr uint16_t TARGET_AFFECTED_NODES = 40;

    // Politeness factor for the one-hop extension check in the hop walk
    static constexpr float POLITENESS_FACTOR = 1.5f;

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
    /// Marks the current hour as seen and updates the stored hop count to the minimum observed.
    /// Triggers a trim pass if the list exceeds FILL_HIGH_PCT after the insertion.
    void sampleRxPacket(uint32_t nodeId, uint8_t hopCount);

    /// Perform hourly rollover.
    /// 1. Tallies per-hop counts for entries matching filteringDenominator and seen in 12 h.
    /// 2. Checks for scale-down (< FILL_LOW_PCT of capacity pass filteringDenominator).
    /// 3. Drops filteringDenominator to samplingDenominator if the 12-h hold has expired.
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
#endif

  private:
    // -----------------------------------------------------------------------
    // Storage
    // -----------------------------------------------------------------------
    HistogramEntry entries[CAPACITY] = {};
    uint8_t count = 0;

    // -----------------------------------------------------------------------
    // Denominator state
    // -----------------------------------------------------------------------
    uint8_t samplingDenominator = DENOM_MIN;  // Current ingest filter (can go up and down)
    uint8_t filteringDenominator = DENOM_MIN; // Current count filter (held high for 12 h)
    uint32_t filteringDenomElevatedAt = 0;    // nowMs() when filteringDenominator was last raised

    // -----------------------------------------------------------------------
    // Cached hourly results
    // -----------------------------------------------------------------------
    PerHopCounts lastPerHopCounts = {};
    uint8_t lastSuggestedHop = MAX_HOP;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Remove stale entries (seen-bits all zero) and, if the list is still crowded,
    /// double samplingDenominator and filteringDenominator and remove non-matching entries.
    void trimIfNeeded();

    /// Count entries whose nodeId passes (nodeId & (filteringDenominator-1)) == 0.
    uint8_t countPassingFilter() const;

    /// Linear scan for an existing entry with this nodeId. Returns nullptr if absent.
    HistogramEntry *findEntry(uint32_t nodeId);

    /// Walk hop buckets scaled by filteringDenominator; return recommended hop limit.
    uint8_t walkHopBuckets(const PerHopCounts &counts) const;

    // -----------------------------------------------------------------------
    // Bitfield helpers (all inline, no heap)
    // -----------------------------------------------------------------------

    // Bitfield layout:
    //   bits [3:0]  → hop count (4 bits, 0–7 meaningful)
    //   bits [15:4] → hourly seen bitmap (12 bits)
    //                 bit 4  = seen in the current / most-recent hour
    //                 bit 15 = seen 11 hours ago
    //                 After each rollHour() the mask shifts left; bit 4 is cleared for the
    //                 new hour.  An entry with all 12 bits clear has not been seen in 12 h.

    static uint16_t seenBits(uint16_t bf) { return (bf >> 4) & 0x0FFFu; }
    static uint8_t hopBits(uint16_t bf) { return bf & 0x0Fu; }
    static uint16_t packBf(uint16_t seen, uint8_t hops) { return static_cast<uint16_t>((seen & 0x0FFFu) << 4) | (hops & 0x0Fu); }
    static bool seenInLast12h(uint16_t bf) { return (bf & 0xFFF0u) != 0u; }
    static void markCurrentHour(uint16_t &bf) { bf |= static_cast<uint16_t>(1u << 4); }
    static uint16_t rollSeenBits(uint16_t bf)
    {
        const uint16_t shifted = static_cast<uint16_t>((seenBits(bf) << 1u) & 0x0FFFu);
        return packBf(shifted, hopBits(bf));
    }

    /// True when (nodeId & (denom-1)) == 0.  Always true when denom == 1.
    static bool passesFilter(uint32_t nodeId, uint8_t denom) { return (nodeId & static_cast<uint32_t>(denom - 1u)) == 0u; }

    // -----------------------------------------------------------------------
    // Clock
    // -----------------------------------------------------------------------
#ifdef UNIT_TEST
    static uint32_t nowMs() { return s_testNowMs; }
#else
    static uint32_t nowMs() { return millis(); }
#endif
};
