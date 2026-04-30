#pragma once

#include "MeshTypes.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/mesh-pb-constants.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

#if HAS_VARIABLE_HOPS

/**
 * HopScalingModule: Sampled hop-distance histogram for mesh-aware hop limit recommendations.
 *
 * Memory layout: 512 bytes total (128 entries × 4 bytes/entry, no padding)
 *   - 16-bit XOR-fold hash of node ID
 *   - 3-bit hops away (0–7)
 *   - 13-bit hourly seen bitmap
 *   All three fields are packed into a single 32-bit Record; sizeof(Record) == 4.
 *
 * Sampling:
 *   - A node is added only when passesFilter(hashNodeId(nodeId), samplingDenominator),
 *     i.e. (hash16(nodeId) & (samplingDenominator – 1)) == 0  (hash-space subsample, not raw ID)
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
 *
 * Thread-safety: all access is single-threaded via the main loop cooperative scheduler.
 */

struct Record {
    uint32_t nodeHash : 16;
    uint32_t hops_away : 3;
    uint32_t seenHoursAgo : 13;
};
static_assert(sizeof(Record) == 4);

class HopScalingModule : private concurrency::OSThread
{
  public:
    // -----------------------------------------------------------------------
    // Capacity and memory layout
    // -----------------------------------------------------------------------
    static constexpr size_t CAPACITY = 128;
    static constexpr size_t ENTRY_BYTES = sizeof(Record);
    static constexpr size_t TOTAL_BYTES = CAPACITY * ENTRY_BYTES;

    // Denominator limits (must be powers of 2)
    static constexpr uint8_t DENOM_MIN = 1;
    static constexpr uint8_t DENOM_MAX = 128;

    static constexpr uint8_t MAX_HOP = 7;

    // Fill-level thresholds (percent of CAPACITY)
    static constexpr uint8_t FILL_HIGH_PCT = 80;
    static constexpr uint8_t FILL_LOW_PCT = 20;

    // How long filteringDenominator is held at an elevated level before it may drop.
    //
    // This value is deliberately equal to the seenHoursAgo window (13 hours / 13 bits).
    // Invariant: every entry that existed when a scale-up fired had seenHoursAgo != 0 at
    // that moment (trimIfNeeded() evicts stale entries before doubling the denominator),
    // so it remains seenInLast13h for at most 13 more rollHour() calls — exactly the
    // hold duration.  That means entries from the scale-up event keep counts.total above
    // the scale-down threshold for the entire hold period under normal (active) mesh
    // conditions.  On a genuinely quieting mesh the scale-down CAN fire before the hold
    // expires — each firing halves samplingDenominator but filteringDenominator stays
    // elevated, so the hop recommendation correctly stays conservative (MAX_HOP) while
    // the cascade runs.  The cascade is bounded at DENOM_MIN (7 halvings from DENOM_MAX);
    //    when the hold finally expires, step 5 of rollHour() halves filteringDenominator
    //    once per hour (rather than jumping directly to samplingDenominator) until the two
    //    converge, giving the hop-walk a gradual, 1-step-per-hour descent.
    static constexpr uint32_t FILTER_DENOM_HOLD_MS = 13UL * 60UL * 60UL * 1000UL; // 13 h (documentation only)
    // Number of rollHour() calls the hold spans — equals the seenHoursAgo window width.
    // filteringDenomHoldRollsRemaining is initialised to this value on scale-up and
    // decremented once per rollHour(); step-down begins when it reaches zero.
    static constexpr uint8_t FILTER_DENOM_HOLD_ROLLS = 13u;

    // Hop-walk: target cumulative affected-node count when choosing a hop limit
    static constexpr uint16_t TARGET_AFFECTED_NODES = 40;

    // Politeness factors for the one-hop extension check in the hop walk
    static constexpr float POLITENESS_GENEROUS = 2.0f; // Quiet mesh: allow doubling
    static constexpr float POLITENESS_DEFAULT = 1.5f;  // Stable mesh: allow 50% growth
    static constexpr float POLITENESS_STRICT = 1.25f;  // Busy mesh: allow 25% growth

    // Activity weight thresholds (ratio of 0-2 h window vs 1-3 h window)
    static constexpr float ACTIVITY_WEIGHT_GENEROUS_MAX = 0.9f; // Below this: GENEROUS
    static constexpr float ACTIVITY_WEIGHT_STRICT_MIN = 1.2f;   // Above this: STRICT

    // Scheduling: number of 5-minute runOnce() ticks that make up one hourly rollover
    static constexpr uint8_t RUNS_PER_HOUR = 12;

    // -----------------------------------------------------------------------
    // Types
    // -----------------------------------------------------------------------

    /// Per-hop node counts produced at each hourly rollover.
    struct PerHopCounts {
        uint16_t perHop[MAX_HOP + 1] = {};
        uint16_t total = 0;
    };

    /// Mesh activity trend stats produced at each hourly rollover.
    /// All counts are scaled by filteringDenominator (i.e. estimated full-mesh population).
    ///
    /// Bitmap interpretation (before the hourly shift): bit 0 = just-completed hour, bit 12 = 12 h ago.
    struct MeshTrendStats {
        /// Estimated node count per hour slot (h=0 is the just-completed hour, h=12 is 12 h ago).
        uint16_t scaledPerHour[13] = {};
        /// Nodes heard only this hour with no prior bitmap history — indicates new arrivals.
        uint16_t newThisHour = 0;
        /// Nodes heard this hour that also appeared in at least one older hour — stable regulars.
        uint16_t returningThisHour = 0;
        /// Nodes heard last hour but silent this hour — potential departures.
        uint16_t lapsedSinceLastHour = 0;
        /// Nodes absent from the last 4 hours but still present in some older hour (5–13 h) — quieting down.
        uint16_t olderThan4h = 0;
        /// Nodes whose only remaining history is the 13th hour (bit 12 only) — about to age out entirely.
        uint16_t agingOut = 0;
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    HopScalingModule();
    ~HopScalingModule() = default;

    /// Reset all entries and state.
    void clear();

    // -----------------------------------------------------------------------
    // Core API
    // -----------------------------------------------------------------------

    /// Record a received packet.
    /// Adds or updates an entry when passesFilter(hashNodeId(nodeId), samplingDenominator),
    /// i.e. when the 16-bit XOR-fold hash of the node ID falls in the 1/samplingDenominator
    /// subsample of the hash space.  This is NOT a raw nodeId modulo check.
    /// Marks the current hour as seen and updates the stored hop count to the last observed value.
    /// Triggers a trim pass if the list exceeds FILL_HIGH_PCT after the insertion.
    void samplePacketForHistogram(uint32_t nodeId, uint8_t hopCount);

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    uint8_t getLastRequiredHop() const { return lastRequiredHop; }
    uint8_t getEntryCount() const { return count; }
    uint8_t getFillPercentage() const { return static_cast<uint8_t>((static_cast<uint16_t>(count) * 100u) / CAPACITY); }
    uint8_t getSamplingDenominator() const { return samplingDenominator; }
    uint8_t getFilteringDenominator() const { return filteringDenominator; }
    float getPoliteness() const { return lastPoliteness; }
    const PerHopCounts &getLastPerHopCounts() const { return lastPerHopCounts; }
    uint8_t getLastSuggestedHop() const { return lastSuggestedHop; }
    const MeshTrendStats &getLastTrendStats() const { return lastTrendStats; }

    // Compatibility accessors used by tests
    uint8_t getCompactHistogramEntryCount() const { return getEntryCount(); }
    uint8_t getCompactHistogramDenominator() const { return getSamplingDenominator(); }
    uint8_t getCompactHistogramFilterDenominator() const { return getFilteringDenominator(); }
    uint8_t getCompactHistogramSuggestedHop() const { return getLastSuggestedHop(); }
    size_t getCompactHistogramAllSampleCount() const { return getEntryCount(); }

    /// Force both sampling and filtering denominators to a specific value.
    /// Intended for unit tests that need a deterministic starting denominator.
    void setSamplingDenominator(uint8_t d)
    {
        samplingDenominator = (d < DENOM_MIN) ? DENOM_MIN : (d > DENOM_MAX ? DENOM_MAX : d);
        filteringDenominator = samplingDenominator;
        filteringDenomHoldRollsRemaining = 0;
    }

#ifdef UNIT_TEST
    // Writable from tests as HopScalingModule::s_testNowMs; drives nowMs() in UNIT_TEST builds.
    inline static uint32_t s_testNowMs = 0;
    /// Override the per-session hash seed. Use in tests that need a specific sampling distribution.
    void setHashSeed(uint16_t seed) { hashSeed = seed; }
    uint16_t getHashSeed() const { return hashSeed; }
    /// Expose hashNodeId for tests that need to compute which node IDs pass a given denominator.
    uint16_t hashNodeIdPublic(uint32_t nodeId) const { return hashNodeId(nodeId); }
#endif

  protected:
    int32_t runOnce() override;

  private:
#ifdef UNIT_TEST
    friend class HopScalingTestShim;
#endif

    /// Perform hourly rollover.
    /// 1. Tallies per-hop counts for entries matching filteringDenominator and seen in 13 h.
    /// 2. Walks the scaled hop buckets and returns the recommended hop limit.
    /// 3. Logs scaled per-hop counts and recommendation.
    /// 4. Checks for scale-down (< FILL_LOW_PCT of capacity pass filteringDenominator).
    /// 5. Decrements filteringDenomHoldRollsRemaining (if > 0); once it reaches zero, halves
    ///    filteringDenominator once toward samplingDenominator per rollHour() call.
    /// 6. Shifts all seen bitmaps left by one hour slot.
    void rollHour();
    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /// Persist the histogram state (entries, denominators, hold-timer) to flash.
    /// No-op on platforms without a filesystem.  Performs a full delete-and-rewrite of
    /// the state file on each call; avoid calling more frequently than once per rollHour().
    void saveToDisk() const;

    /// Restore histogram state from flash.  Safe to call even when no file exists.
    /// Call once after construction, before the first rollHour(), to warm-start the
    /// histogram across reboots without waiting 13 hours for data to re-accumulate.
    /// The restored entries are available immediately for sampling, but the first
    /// rollHour() (triggered by the second runOnce() tick) is needed before a warm-start
    /// recommendation replaces the HOP_MAX boot default.
    void loadFromDisk();

    /// Remove stale entries (seen-bits all zero) and, if the list is still crowded,
    /// double samplingDenominator and filteringDenominator and remove non-matching entries.
    void trimIfNeeded();

    void logStatusReport(bool didHourlyUpdate) const;

    // -----------------------------------------------------------------------
    // Histogram storage
    // -----------------------------------------------------------------------
    Record entries[CAPACITY] = {};
    uint8_t count = 0;

    // -----------------------------------------------------------------------
    // Denominator state
    //
    // Two separate denominators control two distinct gates:
    //
    //   samplingDenominator  — admission gate.  A node is added/updated only when
    //     passesFilter(hash, samplingDenominator).  Lower value = more permissive =
    //     more nodes enter = represents recent mesh state.
    //
    //   filteringDenominator — counting gate.  The hop-walk tally in rollHour() only
    //     counts entries that pass passesFilter(hash, filteringDenominator).  It moves
    //     up with samplingDenominator immediately (scale-up) but is held at the
    //     elevated value for FILTER_DENOM_HOLD_MS (13 h) after any scale-up before it
    //     may drop back down (scale-down).
    //
    // Why the estimate is invariant: passesFilter uses a hash-based uniform subsample.
    // For any two powers-of-two denominators D ≤ F, the fraction of D-sampled entries
    // that also pass F is exactly D/F.  Therefore:
    //   raw_count × F  =  (total × D/F) × F  =  total × D
    // The population estimate is the same whether we count with D or with F.
    // The hold period is not about accuracy — it is about stability: it prevents the
    // hop recommendation from reacting to recently-admitted nodes that have not yet
    // accumulated enough seenHoursAgo history to be statistically reliable.
    //
    //   denominatorHistory[h]  — the filteringDenominator used to both gate and scale
    //     hourlyRaw[h].  Invariant: denominatorHistory[h] always equals the
    //     filteringDenominator that was active when seenHoursAgo bit h was set.
    //     rollHour() advances the array at the very start (before the tally loop), then
    //     gates hourlyRaw[h] per-slot by denominatorHistory[h] — each slot's raw count
    //     and multiplier are therefore always consistent, even when filteringDenominator
    //     changes between rolls (e.g. hold expiry).  On scale-up (trimIfNeeded()), the
    //     entire array is backfilled uniformly with the new filteringDenominator to
    //     preserve the invariant retroactively for all 13 slots.  Initialised to
    //     DENOM_MIN (1); scaledPerHour slots that draw from a 1 entry are unscaled —
    //     correct for a fresh instance with no prior history.
    // -----------------------------------------------------------------------
    uint8_t samplingDenominator = DENOM_MIN;
    uint8_t filteringDenominator = DENOM_MIN;
    uint8_t filteringDenomHoldRollsRemaining = 0; // counts down from FILTER_DENOM_HOLD_ROLLS to 0; step-down fires at 0
    uint8_t denominatorHistory[13] = {};
    uint16_t hashSeed = 0;

    // -----------------------------------------------------------------------
    // Cached hourly results
    // -----------------------------------------------------------------------
    PerHopCounts lastPerHopCounts = {};
    uint16_t lastScaledPerHop[MAX_HOP + 1] = {};
    uint8_t lastSuggestedHop = MAX_HOP;
    float lastPoliteness = POLITENESS_DEFAULT;
    MeshTrendStats lastTrendStats = {};

    // -----------------------------------------------------------------------
    // Hop recommendation state
    // -----------------------------------------------------------------------
    uint8_t lastRequiredHop = HOP_MAX;
    uint8_t histogramRollCount = 0;

    // -----------------------------------------------------------------------
    // Scheduler state
    // -----------------------------------------------------------------------
    bool hasCompletedInitialRun = false;
    uint8_t runsSinceLastHourlyUpdate = 0;

    // -----------------------------------------------------------------------
    // Inline record helpers
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
    static bool passesFilter(uint16_t nodeHash, uint8_t denom) { return (nodeHash & static_cast<uint16_t>(denom - 1u)) == 0u; }

  public:
    // Clock — public so tests can share the same timebase via HopScalingModule::s_testNowMs
#ifdef UNIT_TEST
    static uint32_t nowMs() { return s_testNowMs; }
#else
    static uint32_t nowMs() { return millis(); }
#endif
};

extern HopScalingModule *hopScalingModule;

#endif
