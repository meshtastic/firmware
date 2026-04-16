#pragma once

#include "MeshTypes.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#if HAS_TRAFFIC_MANAGEMENT

class SphereOfInfluenceModule : private concurrency::OSThread
{
  public:
    SphereOfInfluenceModule();

    /// Called by NodeDB when it evicts a node to make room for a new one.
    void recordEviction();

    uint8_t getLastRequiredHop() const { return lastRequiredHop; }
    float getLastActivityWeight() const { return lastActivityWeight; }
    float getLastScaleFactor() const { return lastScaleFactor; }
    uint16_t getEvictionsCurrentHour() const { return evictionsCurrentHour; }
    float getRollingEvictionAverage() const { return rollingEvictionAvg12h; }

  protected:
    int32_t runOnce() override;

  private:
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
    uint8_t computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const;
    void rollEvictionHour();

    // Hop recommendation state
    uint8_t lastRequiredHop = HOP_MAX;
    float lastActivityWeight = 1.0f;
    float lastScaleFactor = 1.0f;

    // Eviction tracking: EMA of hourly eviction counts (12h half-life)
    uint16_t evictionsCurrentHour = 0;  // accumulates between rollovers
    float rollingEvictionAvg12h = 0.0f; // EMA updated each hour
};

extern SphereOfInfluenceModule *sphereOfInfluenceModule;

#endif
