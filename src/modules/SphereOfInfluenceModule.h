#pragma once

#include "MeshTypes.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#if HAS_VARIABLE_HOPS

class SphereOfInfluenceModule : private concurrency::OSThread
{
  public:
    SphereOfInfluenceModule();

    /// Called by NodeDB when it evicts a node to make room for a new one.
    void recordEviction();

    /// Called from the packet receive path to feed the sampling-based mesh size estimator.
    /// Only nodes whose ID passes the modulo filter are tracked (1-in-SAMPLING_DENOMINATOR).
    void recordPacketSender(uint32_t nodeId);

    uint8_t getLastRequiredHop() const { return lastRequiredHop; }
    float getLastActivityWeight() const { return lastActivityWeight; }
    float getLastScaleFactor() const { return lastScaleFactor; }
    uint16_t getEvictionsCurrentHour() const { return evictionsCurrentHour; }
    float getRollingEvictionAverage() const { return rollingEvictionAvg12h; }

  protected:
    int32_t runOnce() override;

  private:
    // Modulo sampling: track 1-in-N unique node IDs from the packet stream
    // TODO make this scale the sample rate dynamically based on observed mesh size and turnover, to keep the estimator accurate
    // without excessive memory use
    static constexpr uint8_t SAMPLING_DENOMINATOR = 10;
    static constexpr uint16_t SAMPLE_TRACKER_SLOTS = 128; // power of 2 for fast modulo

    /// Open-addressing hash set for deduplicating sampled node IDs within one hour.
    /// Capacity is SAMPLE_TRACKER_SLOTS; collisions are resolved by linear probing.
    /// At 75% load factor cap (96 slots), supports meshes of ~960 nodes at 1-in-10 sampling.
    struct SampleTracker {
        uint32_t slots[SAMPLE_TRACKER_SLOTS];
        uint16_t uniqueCount;

        void clear();
        bool record(uint32_t nodeId); // returns true if newly added
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
    float estimateScaleFactor(const Snapshot &snapshot) const;
    uint16_t estimateSampledMeshSize() const;
    uint8_t computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const;
    void rollEvictionHour();

    // Hop recommendation state
    uint8_t lastRequiredHop = HOP_MAX;
    float lastActivityWeight = 1.0f;
    float lastScaleFactor = 1.0f;

    // Eviction tracking: EMA of hourly eviction counts (12h half-life)
    uint16_t evictionsCurrentHour = 0;  // accumulates between rollovers
    float rollingEvictionAvg12h = 0.0f; // EMA updated each hour

    // Sampling-based mesh size estimation for high-turnover scenarios
    SampleTracker sampledNodesCurrentHour = {};
    float rollingSampledAvg12h = 0.0f; // EMA of unique sampled nodes per hour
};

extern SphereOfInfluenceModule *sphereOfInfluenceModule;

#endif
