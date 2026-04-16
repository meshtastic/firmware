#include "SphereOfInfluenceModule.h"

#if HAS_VARIABLE_HOPS

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
constexpr float NODEDB_NEAR_CAPACITY_RATIO = 0.9f;    // 90% of MAX_NUM_NODES triggers scaling
constexpr float TURNOVER_CAPACITY_LIMIT_RATIO = 0.2f; // Evictions below 20% of MAX = limit of turnover scaling

// Modulo sampling: track 1-in-N unique node IDs from the packet stream
// Dynamically adjusted to keep sample tracker load between 1/8 and 5/8 capacity
uint8_t SAMPLING_DENOMINATOR = 10; // mutable (was: constexpr)
constexpr uint8_t SAMPLING_DENOMINATOR_MIN = 1;
constexpr uint8_t SAMPLING_DENOMINATOR_MAX = 200;
} // namespace

SphereOfInfluenceModule *sphereOfInfluenceModule;

void SphereOfInfluenceModule::HopBucket::clear()
{
    memset(perHop, 0, sizeof(perHop));
    unknownHopCount = 0;
    total = 0;
}

/// Classify a node into its hop distance bucket.
/// Nodes with a valid hops_away (0..HOP_MAX) increment the per-hop histogram;
/// nodes without hop info are counted separately so they don't skew the distribution.
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

/// Called by NodeDB each time a node is evicted to make room for a new one.
/// The count is rolled into a 12h EMA once per hour by rollHour().
void SphereOfInfluenceModule::recordEviction()
{
    evictionsCurrentHour++;
}

void SphereOfInfluenceModule::SampleTracker::clear()
{
    memset(slots, 0, sizeof(slots));
    uniqueCount = 0;
}

/// Insert a node ID into the hash set. Returns true if the ID was newly added.
/// Uses open-addressing with linear probing; slot 0 value 0 is reserved as "empty".
bool SphereOfInfluenceModule::SampleTracker::record(uint32_t nodeId)
{
    if (nodeId == 0)
        return false; // 0 is the empty sentinel
    if (uniqueCount >= SAMPLE_TRACKER_SLOTS * 3 / 4)
        return false; // load factor > 75%, stop inserting to avoid long probe chains

    uint16_t idx = static_cast<uint16_t>(nodeId) & (SAMPLE_TRACKER_SLOTS - 1);
    for (uint16_t probe = 0; probe < SAMPLE_TRACKER_SLOTS; ++probe) {
        if (slots[idx] == 0) {
            slots[idx] = nodeId;
            uniqueCount++;
            return true;
        }
        if (slots[idx] == nodeId)
            return false; // already recorded
        idx = (idx + 1) & (SAMPLE_TRACKER_SLOTS - 1);
    }
    return false; // table full (shouldn't happen with load factor check)
}

/// Record a packet sender for sampling-based mesh size estimation.
/// Only 1-in-SAMPLING_DENOMINATOR nodes are tracked (deterministic by node ID).
void SphereOfInfluenceModule::recordPacketSender(uint32_t nodeId)
{
    if (nodeId % SAMPLING_DENOMINATOR == 0) {
        sampledNodesCurrentHour.record(nodeId);
    }
}

/// Extrapolate total mesh size from the sampled unique node count.
/// Returns 0 if too few samples to be reliable (caller should fall back to NodeDB count).
uint16_t SphereOfInfluenceModule::estimateSampledMeshSize() const
{
    if (rollingSampledAvg12h < 2.0f)
        return 0; // EMA of estimated mesh size too low to be reliable
    return static_cast<uint16_t>(std::min(rollingSampledAvg12h, 65535.0f));
}

void SphereOfInfluenceModule::rollHour()
{
    // EMA with alpha = 1/12: approximates a 12h rolling average
    // newAvg = oldAvg * (11/12) + currentHour * (1/12)
    rollingEvictionAvg12h = rollingEvictionAvg12h * (11.0f / 12.0f) + evictionsCurrentHour * (1.0f / 12.0f);
    evictionsCurrentHour = 0;

    // Roll estimated mesh size into 12h EMA using the denominator active this hour, before any adjustment.
    // Storing the mesh estimate (rather than the raw sample count) ensures the EMA remains meaningful
    // across denominator changes — each hour's contribution is already in absolute node counts.
    const float estimatedMeshThisHour = sampledNodesCurrentHour.uniqueCount * SAMPLING_DENOMINATOR;
    rollingSampledAvg12h = rollingSampledAvg12h * (11.0f / 12.0f) + estimatedMeshThisHour * (1.0f / 12.0f);

    // Adaptive sampling: adjust denominator based on sample tracker load factor.
    // Target: keep tracker between 1/8 and 5/8 of the 75% load factor cap to avoid hash collisions
    // while maintaining enough samples for a reliable estimate.
    const float samplesThisHour = static_cast<float>(sampledNodesCurrentHour.uniqueCount);
    const float maxUseful = static_cast<float>(SAMPLE_TRACKER_SLOTS) * (3.0f / 4.0f); // 75% load factor cap
    const float loadRatio = samplesThisHour / maxUseful;

    if (loadRatio > 5.0f / 8.0f && SAMPLING_DENOMINATOR < SAMPLING_DENOMINATOR_MAX) {
        // Tracker getting too full; reduce sample rate (increase denominator)
        SAMPLING_DENOMINATOR = static_cast<uint8_t>(
            std::min(static_cast<uint16_t>(SAMPLING_DENOMINATOR) * 2, static_cast<uint16_t>(SAMPLING_DENOMINATOR_MAX)));
        LOG_INFO("[SOI] Adaptive sampling: denominator increased to %u (load ratio=%.2f)", SAMPLING_DENOMINATOR,
                 static_cast<double>(loadRatio));
    } else if (loadRatio < 1.0f / 8.0f && SAMPLING_DENOMINATOR > SAMPLING_DENOMINATOR_MIN) {
        // Tracker underutilized; increase sample rate (decrease denominator)
        SAMPLING_DENOMINATOR = static_cast<uint8_t>(
            std::max(static_cast<uint16_t>(SAMPLING_DENOMINATOR / 2), static_cast<uint16_t>(SAMPLING_DENOMINATOR_MIN)));
        LOG_INFO("[SOI] Adaptive sampling: denominator decreased to %u (load ratio=%.2f)", SAMPLING_DENOMINATOR,
                 static_cast<double>(loadRatio));
    }

    sampledNodesCurrentHour.clear();
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
    // Ratio of overlapping activity windows: nodes seen within the last 0-2h
    // versus nodes seen within 1-3h. Using the shared 1-2h bucket smooths the
    // comparison across hourly boundaries while still indicating whether activity
    // is skewing more recent or older. High ratio => relatively more recent
    // activity; low => activity is aging out.
    const uint32_t recentActiveNodes = snapshot.recent1h.total + snapshot.old1hFrom2h.total;
    const uint32_t olderActiveNodes = snapshot.old1hFrom2h.total + snapshot.old1hFrom3h.total;

    // Not enough historical data to compute a meaningful ratio. Use a neutral
    // fallback so hop selection remains stable instead of dividing by zero or
    // treating a non-positive denominator as valid.
    if (olderActiveNodes <= 1 || recentActiveNodes <= 1) {
        return 1.0f;
    }
    const float activityWeight = static_cast<float>(recentActiveNodes) / static_cast<float>(olderActiveNodes);
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
    const float nearCapacity = NODEDB_NEAR_CAPACITY_RATIO * MAX_NUM_NODES;
    if (!nodeDB || (!nodeDB->isFull() && snapshot.cumulative12h.total < nearCapacity)) {
        LOG_DEBUG("[SOI] scaleFactor=1.0 (DB has headroom: %u/%d nodes)", snapshot.cumulative12h.total, MAX_NUM_NODES);
        return 1.0f;
    }

    // NodeDB at or near capacity — use rolling 12h eviction average to decide scale.
    if (rollingEvictionAvg12h < (TURNOVER_CAPACITY_LIMIT_RATIO * MAX_NUM_NODES)) {
        // Low turnover: NodeDB is full but few evictions; modest scale-up.
        // Scale proportionally: at 0 evictions => 1.0, approaching threshold => 1.2^12
        const float t = (1.0f + (rollingEvictionAvg12h / (MAX_NUM_NODES)));
        const float scale = powf(t, 12.0f);
        LOG_DEBUG("[SOI] scaleFactor=%.2f (low turnover: evict/h=%.1f, t=%.3f)", static_cast<double>(scale),
                  static_cast<double>(rollingEvictionAvg12h), static_cast<double>(t));
        return scale;
    }

    // High turnover: many evictions per hour, NodeDB is cycling rapidly.
    // Use sampling-based estimate if available; otherwise fall back to eviction-pressure scaling.
    const uint16_t sampledEstimate = estimateSampledMeshSize();
    if (sampledEstimate > 8.8f * MAX_NUM_NODES) {
        // Scale = estimated true mesh size / maximum NodeDB count
        const float scale = std::max(static_cast<float>(sampledEstimate) / MAX_NUM_NODES, 1.0f);
        LOG_DEBUG("[SOI] scaleFactor=%.2f (sampled: est=%u nodes, DB=%d)", static_cast<double>(scale), sampledEstimate,
                  MAX_NUM_NODES);
        return scale;
    } else {
        // Fallback to eviction rate but scale more aggressively, over the full capacity turnover
        const float t = 1.1f + (rollingEvictionAvg12h / (MAX_NUM_NODES));
        const float scale = powf(t, 12.0f);
        LOG_DEBUG("[SOI] scaleFactor=%.2f (high turnover fallback: evict/h=%.1f, sampled=%u, t=%.3f)", static_cast<double>(scale),
                  static_cast<double>(rollingEvictionAvg12h), sampledEstimate, static_cast<double>(t));
        return scale;
    }
}

uint8_t SphereOfInfluenceModule::computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const
{
    uint16_t affectedNodesPerHop[HOP_MAX + 1];
    memset(affectedNodesPerHop, 0, sizeof(affectedNodesPerHop));

    for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
        // Average the three 1h buckets down to a single-hour node count at this hop.
        const uint16_t avg1hFrom3h = static_cast<uint16_t>(
            (snapshot.recent1h.perHop[hop] + snapshot.old1hFrom2h.perHop[hop] + snapshot.old1hFrom3h.perHop[hop]) / 3);

        // 12h cumulative is the best population estimate; scaleFactor compensates for eviction undercount.
        const float scaled12h = snapshot.cumulative12h.perHop[hop] * scaleFactor;
        const uint16_t scaled12hInt = static_cast<uint16_t>(std::min(scaled12h, static_cast<float>(UINT16_MAX)));

        // If there isn't much action in the 12 hour window, or there is a flurry of activity in the recent 1-3h average, use the
        // recent average to avoid underestimation. Otherwise, use the scaled 12h count for a more stable estimate of the affected
        // nodes.
        affectedNodesPerHop[hop] = std::max(avg1hFrom3h, scaled12hInt);
    }

    LOG_INFO("[SOI] perHop raw:    [%u %u %u %u %u %u %u %u]", snapshot.cumulative12h.perHop[0], snapshot.cumulative12h.perHop[1],
             snapshot.cumulative12h.perHop[2], snapshot.cumulative12h.perHop[3], snapshot.cumulative12h.perHop[4],
             snapshot.cumulative12h.perHop[5], snapshot.cumulative12h.perHop[6], snapshot.cumulative12h.perHop[7]);
    LOG_INFO("[SOI] perHop scaled: [%u %u %u %u %u %u %u %u]", affectedNodesPerHop[0], affectedNodesPerHop[1],
             affectedNodesPerHop[2], affectedNodesPerHop[3], affectedNodesPerHop[4], affectedNodesPerHop[5],
             affectedNodesPerHop[6], affectedNodesPerHop[7]);

    uint8_t baseHop = HOP_MAX;
    uint32_t affectedNodes = 0;
    for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
        affectedNodes += affectedNodesPerHop[hop];
        if (affectedNodes >= TARGET_AFFECTED_NODES) {
            baseHop = hop;
            break;
        }
    }

    if (baseHop < HOP_MAX) {
        const uint32_t affectedIfExtend = affectedNodes + affectedNodesPerHop[baseHop + 1];
        const float politeLimit = affectedNodes * politenessFactor;
        if (affectedIfExtend <= politeLimit) {
            baseHop++;
        }
    }

    return baseHop;
}

int32_t SphereOfInfluenceModule::runOnce()
{
    static bool hasCompletedInitialRun = false;

    // The first run is scheduled after INITIAL_DELAY_MS, not RUN_INTERVAL_MS,
    // so do not roll the hourly eviction bucket until subsequent hourly runs.
    if (hasCompletedInitialRun) {
        rollHour();
    } else {
        hasCompletedInitialRun = true;
    }
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
