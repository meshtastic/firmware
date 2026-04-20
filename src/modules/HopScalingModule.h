#pragma once

#include "CompactHistogram.h"
#include "MeshTypes.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"

#if HAS_VARIABLE_HOPS

// Thread-safety: all access is single-threaded. recordEviction() and
// recordPacketSender() are called from NodeDB::updateFrom() on the main loop;
// getLastRequiredHop() is called from Router::send() on the same loop; and
// runOnce() is invoked cooperatively by mainController. No preemption occurs
// between these paths, so no locking is required.

class HopScalingModule : private concurrency::OSThread
{
  public:
    HopScalingModule();

    enum StatusMode : uint8_t {
        STATUS_STARTUP_NOT_ENOUGH_DATA = 0,
        STATUS_SMALL_STABLE_MESH = 1,
        STATUS_SCALED_FROM_EVICTION = 2,
        STATUS_SCALED_FROM_MESH_ESTIMATE = 3,
        STATUS_FALLBACK_EVICTION_SCALE = 4,
    };

    /// Called by NodeDB when it evicts a node to make room for a new one.
    void recordEviction();

    /// Called from the packet receive path to feed the sampling-based mesh size estimator.
    /// Only nodes whose ID passes the modulo filter are tracked (1-in-SAMPLING_DENOMINATOR).
    void recordPacketSender(uint32_t nodeId);

    /// Sample incoming packet into the CompactHistogram (bitwise hop scaling).
    /// This is the new parallel sampling mechanism alongside recordPacketSender.
    void samplePacketForHistogram(uint32_t nodeId, uint8_t hopCount);

    /// Enable or disable denominator jitter. Jitter is on by default; disable in unit tests
    /// to keep SAMPLING_DENOMINATOR on power-of-2 values so injectSampleTraffic() works.
    static void setSamplingJitter(bool enabled) { s_samplingJitter = enabled; }
    static bool s_samplingJitter; // true by default; friend-visible to jitterDenominator()

    /// Enable or disable adaptive denominator adjustment. Enabled by default; disable in unit
    /// tests so the denominator stays pinned at its initial value across all scenarios.
    static void setSamplingAdaptation(bool enabled) { s_samplingAdaptationEnabled = enabled; }
    static bool s_samplingAdaptationEnabled;

    /// Reset SAMPLING_DENOMINATOR to its initial value (8). Call in test setUp after pinning
    /// adaptation off, so each test starts with a known denominator regardless of prior state.
    static void resetSamplingDenominator();

    /// Set SAMPLING_DENOMINATOR to an explicit value. For test use only — lets megamesh
    /// scenarios pin the denominator high enough that per-window samples stay under the
    /// 96-slot tracker cap, producing correct estimates without needing adaptive adjustment.
    static void setDenominatorForTest(uint8_t d);

    uint8_t getLastRequiredHop() const { return lastRequiredHop; }
    float getLastActivityWeight() const { return lastActivityWeight; }
    float getLastScaleFactor() const { return lastScaleFactor; }
    uint16_t getEvictionsCurrentHour() const { return evictionsCurrentHour; }
    float getRollingEvictionAverage() const { return rollingEvictionAvg12h; }
    uint8_t getCompactHistogramEntryCount() const { return hopScalingHistogram.getEntryCount(); }
    uint8_t getCompactHistogramDenominator() const { return hopScalingHistogram.getSamplingDenominator(); }
    uint8_t getCompactHistogramFilterDenominator() const { return hopScalingHistogram.getFilteringDenominator(); }
    uint8_t getCompactHistogramSuggestedHop() const { return hopScalingHistogram.getLastSuggestedHop(); }
    size_t getCompactHistogramAllSampleCount() const { return hopScalingHistogram.getEntryCount(); }

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint16_t SAMPLE_TRACKER_SLOTS = 128; // power of 2 for fast modulo
    static constexpr uint16_t SAMPLE_TRACKER_LOAD_CAP = SAMPLE_TRACKER_SLOTS * 3 / 4;

#ifdef UNIT_TEST
    friend class HopScalingTestShim; // grants access to private members for test-only helpers
#endif

    /// Open-addressing hash set for deduplicating sampled node IDs within one hour.
    /// Capacity is SAMPLE_TRACKER_SLOTS; collisions are resolved by linear probing.
    /// At 75% load factor cap (96 slots) with an adaptive denominator targeting 1/8-5/8 load.
    struct SampleTracker {
        uint32_t slots[SAMPLE_TRACKER_SLOTS];
        uint16_t uniqueCount;

        void clear();
        bool record(uint32_t nodeId);
    };

    struct HopBucket {
        uint16_t perHop[HOP_MAX + 1];
        uint16_t unknownHopCount;
        uint16_t total;

        void clear();
        void add(const meshtastic_NodeInfoLite &node);
    };

    struct Snapshot {
        HopBucket recent1h;
        HopBucket old1hFrom2h;
        HopBucket old1hFrom3h;
        HopBucket cumulative12h;
    };

    void buildSnapshot(Snapshot &snapshot) const;
    float computeActivityWeight(const Snapshot &snapshot) const;
    float selectPolitenessFactor(float activityWeight) const;
    float estimateScaleFactor(const Snapshot &snapshot, uint8_t &statusMode, uint16_t &sampledEstimate,
                              uint16_t &evictionEstimate) const;
    uint16_t estimateSampledMeshSize() const;
    uint8_t computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const;
    bool checkStableStatus(const Snapshot &snapshot) const;
    void logStatusReport(const Snapshot &snapshot, bool didHourlyUpdate) const;
    void rollSampleWindow(bool earlyTrigger);
    void adjustSamplingDenominatorForLoad(float loadRatio);
    void rollHour();
    void loadState();
    void saveState() const;

    // Bitwise hop scaling: compact histogram for tracking node hop distances
    // This is a parallel implementation alongside the existing nodeDB-based approach
    CompactHistogram hopScalingHistogram;

    // Hop recommendation state
    uint8_t lastRequiredHop = HOP_MAX;
    float lastActivityWeight = 1.0f;
    float lastScaleFactor = 1.0f;
    float lastPolitenessFactor = 1.5f;
    uint8_t lastStatusMode = STATUS_STARTUP_NOT_ENOUGH_DATA;
    uint16_t lastSampledEstimate = 0;
    uint16_t lastEvictionEstimate = 0;

    // Eviction tracking: smoothed average of hourly eviction counts
    uint16_t evictionsCurrentHour = 0;  // accumulates between rollovers
    float rollingEvictionAvg12h = 0.0f; // updated once per hour

    // Sampling-based mesh size estimation for high-turnover scenarios
    SampleTracker sampledNodesCurrentHour = {};
    float rollingSampledAvg12h = 0.0f; // Rolling average estimate of mesh size per hour (denominator-normalised)
    uint32_t sampleWindowStartMs = 0;
    uint8_t rollingAvgRollCount = 0; // Warm-up counter: ramps alpha from 1/1 to 1/12 over the first 12 rolls

    // Per-instance scheduler state for hourly recomputation cadence.
    bool hasCompletedInitialRun = false;
    uint8_t runsSinceLastHourlyUpdate = 0;
};

extern HopScalingModule *hopScalingModule;

#endif
