#include "SphereOfInfluenceModule.h"

#if HAS_TRAFFIC_MANAGEMENT

#include "NodeDB.h"
#include "mesh-pb-constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
// Time windows for NodeDB age bucketing
constexpr uint32_t ONE_HOUR_SECS = 60 * 60;
constexpr uint32_t TWO_HOURS_SECS = 2 * ONE_HOUR_SECS;
constexpr uint32_t THREE_HOURS_SECS = 3 * ONE_HOUR_SECS;
constexpr uint32_t TWELVE_HOURS_SECS = 12 * ONE_HOUR_SECS;

// Module scheduling
constexpr uint32_t INITIAL_DELAY_MS = 30 * 1000UL;     // Startup grace period before first run
constexpr uint32_t RUN_INTERVAL_MS = 60 * 60 * 1000UL; // Recalculate hop recommendation hourly

// Hop recommendation: cumulative node target before constraining hops
constexpr uint16_t TARGET_AFFECTED_NODES = 40;

// Politeness factors: max ratio of (nodes at hop+1) / (nodes at hop 0..base)
constexpr float POLITENESS_GENEROUS = 2.0f; // Quiet mesh: allow doubling
constexpr float POLITENESS_DEFAULT = 1.5f;  // Stable mesh: allow 50% growth
constexpr float POLITENESS_STRICT = 1.25f;  // Busy mesh: allow 25% growth

// Activity weight thresholds for politeness regime selection
constexpr float ACTIVITY_WEIGHT_GENEROUS_MAX = 0.9f; // Below this: GENEROUS
constexpr float ACTIVITY_WEIGHT_STRICT_MIN = 1.2f;   // Above this: STRICT

// NodeDB capacity thresholds for scale factor estimation
constexpr float NODEDB_NEAR_CAPACITY_RATIO = 0.9f;  // 90% of MAX_NUM_NODES triggers scaling
constexpr float LOW_TURNOVER_CAPACITY_RATIO = 0.5f; // Evictions below 50% of MAX = low turnover
} // namespace

SphereOfInfluenceModule *sphereOfInfluenceModule;

void SphereOfInfluenceModule::HopBucket::clear()
{
    memset(perHop, 0, sizeof(perHop));
    unknownHopCount = 0;
    total = 0;
}

void SphereOfInfluenceModule::HopBucket::add(const meshtastic_NodeInfoLite &node)
{
    total++;
    if (node.has_hops_away && node.hops_away <= HOP_MAX) {
        perHop[node.hops_away]++;
    } else {
        unknownHopCount++;
    }
}

SphereOfInfluenceModule::SphereOfInfluenceModule() : concurrency::OSThread("SphereOfInfluence")
{
    setIntervalFromNow(INITIAL_DELAY_MS);
}

void SphereOfInfluenceModule::recordEviction()
{
    evictionsCurrentHour++;
}

void SphereOfInfluenceModule::rollEvictionHour()
{
    // EMA with alpha = 1/12: approximates a 12h rolling average
    // newAvg = oldAvg * (11/12) + currentHour * (1/12)
    rollingEvictionAvg12h = rollingEvictionAvg12h * (11.0f / 12.0f) + static_cast<float>(evictionsCurrentHour) * (1.0f / 12.0f);
    evictionsCurrentHour = 0;
}

void SphereOfInfluenceModule::buildSnapshot(Snapshot &snapshot) const
{
    snapshot.recent1h.clear();
    snapshot.old1hFrom2h.clear();
    snapshot.old1hFrom3h.clear();
    snapshot.cumulative12h.clear();

    if (!nodeDB) {
        return;
    }

    const size_t nodeCount = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < nodeCount; ++i) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->via_mqtt) {
            continue;
        }

        const uint32_t ageSecs = sinceLastSeen(node);

        if (ageSecs < ONE_HOUR_SECS) {
            snapshot.recent1h.add(*node);
        } else if (ageSecs < TWO_HOURS_SECS) {
            snapshot.old1hFrom2h.add(*node);
        } else if (ageSecs < THREE_HOURS_SECS) {
            snapshot.old1hFrom3h.add(*node);
        }

        if (ageSecs < TWELVE_HOURS_SECS) {
            snapshot.cumulative12h.add(*node);
        }
    }
}

float SphereOfInfluenceModule::computeActivityWeight(const Snapshot &snapshot) const
{
    // Ratio of nodes whose most recent contact was 1-2h ago vs 2-3h ago.
    // Each bucket holds nodes not heard more recently, so these represent departures per window.
    // High ratio => more nodes went quiet recently (mesh churning); low => departures were earlier.
    const float recentChurn = static_cast<float>(snapshot.old1hFrom2h.total);
    const float olderChurn = std::max(1.0f, static_cast<float>(snapshot.old1hFrom3h.total));
    const float activityWeight = recentChurn / olderChurn;
    return activityWeight;
}

float SphereOfInfluenceModule::selectPolitenessFactor(float activityWeight) const
{
    if (activityWeight < ACTIVITY_WEIGHT_GENEROUS_MAX) {
        return POLITENESS_GENEROUS;
    }
    if (activityWeight > ACTIVITY_WEIGHT_STRICT_MIN) {
        return POLITENESS_STRICT;
    }
    return POLITENESS_DEFAULT;
}

float SphereOfInfluenceModule::estimateScaleFactor(const Snapshot &snapshot) const
{
    // Not-full NodeDB with headroom: direct counts are accurate, no scaling needed.
    const float nearCapacity = NODEDB_NEAR_CAPACITY_RATIO * static_cast<float>(MAX_NUM_NODES);
    if (!nodeDB || (!nodeDB->isFull() && snapshot.cumulative12h.total < nearCapacity)) {
        return 1.0f;
    }

    // NodeDB at or near capacity — use rolling 12h eviction average to decide scale.
    if (rollingEvictionAvg12h < (LOW_TURNOVER_CAPACITY_RATIO * static_cast<float>(MAX_NUM_NODES))) {
        // Low turnover: NodeDB is full but few evictions; modest scale-up.
        // Scale proportionally: at 0 evictions => 1.0, approaching threshold => 1.5
        const float threshold = LOW_TURNOVER_CAPACITY_RATIO * static_cast<float>(MAX_NUM_NODES);
        const float t = rollingEvictionAvg12h / std::max(threshold, 1.0f);
        return 1.0f + (0.5f * t);
    }

    // High turnover: many evictions per hour, NodeDB is cycling rapidly.
    // Scale up more aggressively based on eviction pressure.
    const float maxEvictions = static_cast<float>(MAX_NUM_NODES); // theoretical ceiling
    const float t = std::min(rollingEvictionAvg12h / std::max(maxEvictions, 1.0f), 1.0f);
    return 1.5f + (1.5f * t); // range [1.5, 3.0]
}

uint8_t SphereOfInfluenceModule::computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const
{
    uint16_t affectedNodesPerHop[HOP_MAX + 1];
    memset(affectedNodesPerHop, 0, sizeof(affectedNodesPerHop));

    // Convert scaleFactor to fixed-point (×100) so the per-hop loop is integer-only.
    // scaleFactor compensates for eviction undercount in the 12h window (1.0 when DB has headroom).
    const uint16_t scaleFactorX100 = static_cast<uint16_t>(scaleFactor * 100.0f + 0.5f);

    for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
        // Average the three 1h buckets down to a single-hour node count at this hop.
        const uint16_t avg1hFrom3h = static_cast<uint16_t>(
            (snapshot.recent1h.perHop[hop] + snapshot.old1hFrom2h.perHop[hop] + snapshot.old1hFrom3h.perHop[hop]) / 3);

        // 12h cumulative is the best population estimate; scaleFactor compensates for eviction undercount.
        const uint32_t scaled12h = static_cast<uint32_t>(snapshot.cumulative12h.perHop[hop]) * scaleFactorX100 / 100;
        const uint16_t scaled12hInt = static_cast<uint16_t>(std::min(scaled12h, static_cast<uint32_t>(UINT16_MAX)));

        affectedNodesPerHop[hop] = std::max(avg1hFrom3h, scaled12hInt);
    }

    uint8_t baseHop = HOP_MAX;
    uint16_t cumulativeAtBase = 0;
    for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
        cumulativeAtBase = static_cast<uint16_t>(cumulativeAtBase + affectedNodesPerHop[hop]);
        if (cumulativeAtBase >= TARGET_AFFECTED_NODES) {
            baseHop = hop;
            break;
        }
    }

    if (baseHop < HOP_MAX) {
        const uint16_t cumulativeIfExtend = static_cast<uint16_t>(cumulativeAtBase + affectedNodesPerHop[baseHop + 1]);
        const float politeLimit = static_cast<float>(cumulativeAtBase) * politenessFactor;
        if (static_cast<float>(cumulativeIfExtend) <= politeLimit) {
            baseHop++;
        }
    }

    return baseHop;
}

int32_t SphereOfInfluenceModule::runOnce()
{
    // Roll the eviction hour bucket every run (runs hourly)
    rollEvictionHour();

    Snapshot snapshot{};
    buildSnapshot(snapshot);

    lastActivityWeight = computeActivityWeight(snapshot);
    const float politenessFactor = selectPolitenessFactor(lastActivityWeight);
    lastScaleFactor = estimateScaleFactor(snapshot);
    lastRequiredHop = computeRequiredHop(snapshot, lastScaleFactor, politenessFactor);

    LOG_INFO("[SOI] hop=%u actWt=%.2f polite=%.2f scale=%.2f evict/h=%.1f n1=%u n2=%u n3=%u n12=%u", lastRequiredHop,
             static_cast<double>(lastActivityWeight), static_cast<double>(politenessFactor), static_cast<double>(lastScaleFactor),
             static_cast<double>(rollingEvictionAvg12h), snapshot.recent1h.total,
             snapshot.recent1h.total + snapshot.old1hFrom2h.total,
             snapshot.recent1h.total + snapshot.old1hFrom2h.total + snapshot.old1hFrom3h.total, snapshot.cumulative12h.total);

    return RUN_INTERVAL_MS;
}

#endif
