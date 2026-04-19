#include "HopScalingModule.h"

#if HAS_VARIABLE_HOPS

#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include "mesh-pb-constants.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
constexpr uint32_t RUN_INTERVAL_MS = 10 * 60 * 1000UL; // Emit status report every 10 minutes
constexpr uint8_t RUNS_PER_HOUR = 6;                   // 6 x 10-minute runs = 1 hour

// Hop recommendation: cumulative node target before constraining hops
constexpr uint16_t TARGET_AFFECTED_NODES = 40;

// Politeness factors: multiplier applied to the 40-node floor when deciding one-hop extension.
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
uint8_t SAMPLING_DENOMINATOR = 8; // mutable
constexpr uint8_t SAMPLING_DENOMINATOR_MIN = 1;
constexpr uint8_t SAMPLING_DENOMINATOR_MAX = 128;

/// Apply random jitter to a target denominator value.
/// The jittered result lies in [target/2 + 1, target*2 - 1], clamped to [MIN, MAX].
/// This prevents a bad actor from predicting which node IDs will pass the modulo filter.
/// When s_samplingJitter is false (e.g. in unit tests) the target is returned unchanged.
uint8_t jitterDenominator(uint8_t target)
{
    if (!HopScalingModule::s_samplingJitter)
        return target;
    const uint16_t lo = static_cast<uint16_t>(target / 2) + 1;
    const uint16_t hi = static_cast<uint16_t>(target) * 2 - 1;
    const uint16_t loC = std::max(lo, static_cast<uint16_t>(SAMPLING_DENOMINATOR_MIN));
    const uint16_t hiC = std::min(hi, static_cast<uint16_t>(SAMPLING_DENOMINATOR_MAX));
    if (loC >= hiC)
        return static_cast<uint8_t>(loC);
    return static_cast<uint8_t>(loC + static_cast<uint16_t>(rand()) % (hiC - loC + 1));
}
constexpr uint32_t ONE_HOUR_MS = ONE_HOUR_SECS * 1000UL;

// Persistence
constexpr const char *HOP_SCALING_STATE_FILE = "/prefs/hop_scaling.bin";
constexpr const char *LEGACY_SOI_STATE_FILE = "/prefs/soi.bin";
constexpr uint32_t HOP_SCALING_STATE_MAGIC = 0x48534331;        // 'HSC1' (Hop SCaling state)
constexpr uint32_t LEGACY_HOP_SCALING_STATE_MAGIC = 0x534F4931; // 'SOI1' (legacy)
constexpr uint8_t HOP_SCALING_STATE_VERSION = 1;

struct PersistedState {
    uint32_t magic;
    uint8_t version;
    uint8_t samplingDenominator;
    float rollingEvictionAvg12h;
    float rollingSampledAvg12h;
};
} // namespace

HopScalingModule *hopScalingModule;

void HopScalingModule::HopBucket::clear()
{
    memset(perHop, 0, sizeof(perHop));
    unknownHopCount = 0;
    total = 0;
}

/// Classify a node into its hop distance bucket.
/// Nodes with a valid hops_away (0..HOP_MAX) increment the per-hop histogram;
/// nodes without hop info are counted separately so they don't skew the distribution.
void HopScalingModule::HopBucket::add(const meshtastic_NodeInfoLite &node)
{
    total++;
    if (node.has_hops_away && node.hops_away <= HOP_MAX) {
        perHop[node.hops_away]++;
    } else {
        unknownHopCount++;
    }
}

HopScalingModule::HopScalingModule() : concurrency::OSThread("HopScaling")
{
    loadState();
    sampleWindowStartMs = millis();
    setIntervalFromNow(INITIAL_DELAY_MS);
}

/// Called by NodeDB each time a node is evicted to make room for a new one.
/// The count is rolled into a 12h EMA once per hour by rollHour().
void HopScalingModule::recordEviction()
{
    evictionsCurrentHour++;
}

void HopScalingModule::SampleTracker::clear()
{
    memset(slots, 0, sizeof(slots));
    uniqueCount = 0;
}

/// Insert a node ID into the hash set.
/// Uses open-addressing with linear probing; slot 0 value 0 is reserved as "empty".
bool HopScalingModule::SampleTracker::record(uint32_t nodeId)
{
    if (nodeId == 0)
        return false; // 0 is the empty sentinel
    if (uniqueCount >= SAMPLE_TRACKER_LOAD_CAP)
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
void HopScalingModule::recordPacketSender(uint32_t nodeId)
{
    if (nodeId % SAMPLING_DENOMINATOR != 0)
        return;

    sampledNodesCurrentHour.record(nodeId);

    // Trigger an early sample-window roll exactly at the 75% load cap to avoid
    // dropping new IDs and retune sampling while traffic is active.
    if (sampledNodesCurrentHour.uniqueCount >= SAMPLE_TRACKER_LOAD_CAP) {
        rollSampleWindow(true);
    }
}

/// Extrapolate total mesh size from the sampled unique node count.
/// Returns 0 if too few samples to be reliable (caller should fall back to NodeDB count).
uint16_t HopScalingModule::estimateSampledMeshSize() const
{
    if (rollingSampledAvg12h < 2.0f)
        return 0; // Smoothed estimate too low to be reliable
    return static_cast<uint16_t>(std::min(rollingSampledAvg12h, 65535.0f));
}

bool HopScalingModule::s_samplingJitter = true;

void HopScalingModule::adjustSamplingDenominatorForLoad(float loadRatio)
{
    const char *direction = nullptr;
    if (loadRatio > 5.0f / 8.0f && SAMPLING_DENOMINATOR < SAMPLING_DENOMINATOR_MAX) {
        const uint8_t doubled = static_cast<uint8_t>(
            std::min(static_cast<uint16_t>(SAMPLING_DENOMINATOR * 2), static_cast<uint16_t>(SAMPLING_DENOMINATOR_MAX)));
        SAMPLING_DENOMINATOR = jitterDenominator(doubled);
        direction = "increased";
    } else if (loadRatio < 1.0f / 8.0f && SAMPLING_DENOMINATOR > SAMPLING_DENOMINATOR_MIN) {
        const uint8_t halved = static_cast<uint8_t>(
            std::max(static_cast<uint16_t>(SAMPLING_DENOMINATOR / 2), static_cast<uint16_t>(SAMPLING_DENOMINATOR_MIN)));
        SAMPLING_DENOMINATOR = jitterDenominator(halved);
        direction = "decreased";
    }
    if (direction) {
        LOG_INFO("[HOPSCALE] Adaptive sampling: denominator %s to %u (load ratio=%.2f)", direction, SAMPLING_DENOMINATOR,
                 static_cast<double>(loadRatio));
    }
}

void HopScalingModule::rollSampleWindow(bool earlyTrigger)
{
    const uint32_t nowMs = millis();
    const uint32_t windowMs = sampleWindowStartMs ? (nowMs - sampleWindowStartMs) : ONE_HOUR_MS;

    const float samplesThisWindow = static_cast<float>(sampledNodesCurrentHour.uniqueCount);
    const float estimatedMeshThisWindow = samplesThisWindow * SAMPLING_DENOMINATOR;

    // Integrate by the fraction of an hour represented by this window,
    // without exponent-based weighting.
    float windowFraction = std::max(0.0f, std::min(1.0f, static_cast<float>(windowMs) / static_cast<float>(ONE_HOUR_MS)));
    // In unit tests and bursty real traffic, early rolls can occur in the same millisecond
    // as window start (windowMs==0). Apply a small floor so meaningful samples still
    // contribute to the rolling estimate instead of being multiplied by zero.
    if (earlyTrigger) {
        windowFraction = std::max(windowFraction, 0.25f);
    }
    const float alpha = windowFraction * (1.0f / 12.0f);
    rollingSampledAvg12h = rollingSampledAvg12h * (1.0f - alpha) + estimatedMeshThisWindow * alpha;

    const float maxUseful = static_cast<float>(SAMPLE_TRACKER_SLOTS) * (3.0f / 4.0f); // 75% load factor cap
    const float loadRatio = samplesThisWindow / maxUseful;
    adjustSamplingDenominatorForLoad(loadRatio);

    if (earlyTrigger) {
        const bool isOneHourWindow = (windowMs >= ONE_HOUR_MS);
        const char *rollLabel = isOneHourWindow ? "Hourly sample roll" : "Early sample roll";
        LOG_INFO("[HOPSCALE] %s: windowMs=%u unique=%u est=%.1f load=%.2f sampling 1 in %u", rollLabel,
                 static_cast<unsigned int>(windowMs), sampledNodesCurrentHour.uniqueCount,
                 static_cast<double>(estimatedMeshThisWindow), static_cast<double>(loadRatio), SAMPLING_DENOMINATOR);
    }

    sampledNodesCurrentHour.clear();
    sampleWindowStartMs = nowMs;
}

// Roll the hourly eviction count and sampled node count into their respective rolling averages, then reset the hourly
// counters. Also performs adaptive adjustment of the sampling denominator to keep the sample tracker load within optimal bounds.
void HopScalingModule::rollHour()
{
    // Rolling-average update using a fixed 1/12 hourly contribution.
    rollingEvictionAvg12h = rollingEvictionAvg12h * (11.0f / 12.0f) + evictionsCurrentHour * (1.0f / 12.0f);
    evictionsCurrentHour = 0;

    rollSampleWindow(false);
    saveState();
}

// Load persisted state from storage to maintain continuity across reboots. Validates magic, version, and sampling denominator
// before applying.
void HopScalingModule::loadState()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);

    // TODO: REMOVE BEFORE PR SUBMISSION - one-time migration from legacy SoI state filename.
    if (!FSCom.exists(HOP_SCALING_STATE_FILE) && FSCom.exists(LEGACY_SOI_STATE_FILE)) {
        auto legacyFile = FSCom.open(LEGACY_SOI_STATE_FILE, FILE_O_READ);
        if (legacyFile) {
            PersistedState legacyState{};
            if (legacyFile.read((uint8_t *)&legacyState, sizeof(legacyState)) == sizeof(legacyState)) {
                legacyState.magic = HOP_SCALING_STATE_MAGIC;
                legacyState.version = HOP_SCALING_STATE_VERSION;

                auto migratedFile = FSCom.open(HOP_SCALING_STATE_FILE, FILE_O_WRITE);
                if (migratedFile) {
                    migratedFile.write((uint8_t *)&legacyState, sizeof(legacyState));
                    migratedFile.flush();
                    migratedFile.close();
                    LOG_INFO("[HOPSCALE] Migrated legacy state file %s -> %s", LEGACY_SOI_STATE_FILE, HOP_SCALING_STATE_FILE);
                }
            }
            legacyFile.close();
        }
    }

    auto file = FSCom.open(HOP_SCALING_STATE_FILE, FILE_O_READ);
    if (file) {
        PersistedState state{};
        const bool readOk = (file.read((uint8_t *)&state, sizeof(state)) == sizeof(state));
        const bool magicOk = (state.magic == HOP_SCALING_STATE_MAGIC || state.magic == LEGACY_HOP_SCALING_STATE_MAGIC);
        if (readOk && magicOk && state.version == HOP_SCALING_STATE_VERSION &&
            state.samplingDenominator >= SAMPLING_DENOMINATOR_MIN && state.samplingDenominator <= SAMPLING_DENOMINATOR_MAX) {
            rollingEvictionAvg12h = state.rollingEvictionAvg12h;
            rollingSampledAvg12h = state.rollingSampledAvg12h;
            SAMPLING_DENOMINATOR = state.samplingDenominator;
            LOG_INFO("[HOPSCALE] Restored state: evict/h=%.1f sampledAvg=%.1f  sampling 1 in %u",
                     static_cast<double>(rollingEvictionAvg12h), static_cast<double>(rollingSampledAvg12h), SAMPLING_DENOMINATOR);
        } else {
            LOG_DEBUG("[HOPSCALE] No valid persisted state, starting fresh");
        }
        file.close();
    }
#endif
}

// Store the current state to allow continuity across reboots.
void HopScalingModule::saveState() const
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
    if (FSCom.exists(HOP_SCALING_STATE_FILE)) {
        FSCom.remove(HOP_SCALING_STATE_FILE);
    }
    concurrency::LockGuard g(spiLock);
    auto file = FSCom.open(HOP_SCALING_STATE_FILE, FILE_O_WRITE);
    if (file) {
        PersistedState state{};
        state.magic = HOP_SCALING_STATE_MAGIC;
        state.version = HOP_SCALING_STATE_VERSION;
        state.samplingDenominator = SAMPLING_DENOMINATOR;
        state.rollingEvictionAvg12h = rollingEvictionAvg12h;
        state.rollingSampledAvg12h = rollingSampledAvg12h;
        file.write((uint8_t *)&state, sizeof(state));
        file.flush();
        file.close();
        LOG_DEBUG("[HOPSCALE] Saved state: evict/h=%.1f sampledAvg=%.1f  sampling 1 in %u",
                  static_cast<double>(rollingEvictionAvg12h), static_cast<double>(rollingSampledAvg12h), SAMPLING_DENOMINATOR);
    } else {
        LOG_WARN("[HOPSCALE] Failed to open %s for write", HOP_SCALING_STATE_FILE);
    }
#endif
}

// Iterate through NodeDB to build a snapshot of recent activity, classifying nodes into hop buckets and age buckets for analysis.
void HopScalingModule::buildSnapshot(Snapshot &snapshot) const
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

float HopScalingModule::computeActivityWeight(const Snapshot &snapshot) const
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

// Select a politeness factor based on the activity weight. More recent activity (higher weight) => stricter politeness to avoid
// overloading the mesh; older activity (lower weight) => more generous to allow faster propagation and recovery. The default
// mid-range politeness applies when the activity weight is close to 1, indicating a stable mesh with balanced recent
float HopScalingModule::selectPolitenessFactor(float activityWeight) const
{
    if (activityWeight < ACTIVITY_WEIGHT_GENEROUS_MAX) {
        return POLITENESS_GENEROUS;
    }
    if (activityWeight > ACTIVITY_WEIGHT_STRICT_MIN) {
        return POLITENESS_STRICT;
    }
    return POLITENESS_DEFAULT;
}

// Estimate a scale factor to apply to the raw NodeDB counts when computing the required hop. This compensates for the nodeDB
// being an incomplete sample of the whole mesh. Estimation logic tries to adapt to small, medium and megameshes with varying
// turnover patterns by using a combination of heuristics based on NodeDB capacity, eviction rates, and sampling-based mesh size
// estimation.
float HopScalingModule::estimateScaleFactor(const Snapshot &snapshot, uint8_t &statusMode, uint16_t &sampledEstimate) const
{
    float scale = 1.0f; // default (no scaling)
    const uint16_t knownNodeCount = MAX_NUM_NODES - snapshot.cumulative12h.unknownHopCount;
    // Compute sampled estimate for status visibility in all modes.
    // It is only used for scaling decisions in the high-turnover branch below.
    sampledEstimate = estimateSampledMeshSize();

    // Not-full NodeDB with headroom: direct counts are accurate, scaling only for unknown hops needed.
    const float nearCapacity = NODEDB_NEAR_CAPACITY_RATIO * MAX_NUM_NODES;
    if (!nodeDB || (!nodeDB->isFull() && snapshot.cumulative12h.total < nearCapacity)) {
        statusMode = STATUS_SMALL_STABLE_MESH;
        scale = MAX_NUM_NODES / static_cast<float>(knownNodeCount);
        LOG_DEBUG("[HOPSCALE] scaleFactor=%.2f (DB has headroom: %u/%d nodes, of which %.2f unknown hops)",
                  static_cast<double>(scale), snapshot.cumulative12h.total, MAX_NUM_NODES,
                  static_cast<double>(snapshot.cumulative12h.unknownHopCount));
        return scale;
    }

    // NodeDB at or near capacity — use rolling 12h eviction average to decide scale.
    if (rollingEvictionAvg12h < (TURNOVER_CAPACITY_LIMIT_RATIO * MAX_NUM_NODES)) {
        statusMode = STATUS_SCALED_FROM_EVICTION;
        // Low turnover: NodeDB is full but few evictions; modest scale-up.
        // Scale proportionally: at 0 evictions => 1.0, approaching threshold => 1.2^12
        scale = (1.0f + (rollingEvictionAvg12h / knownNodeCount));
        LOG_DEBUG("[HOPSCALE] scaleFactor=%.2f (low turnover: evict/h=%.1f)", static_cast<double>(scale),
                  static_cast<double>(rollingEvictionAvg12h));
        return scale;
    }

    // High turnover: many evictions per hour, NodeDB is cycling rapidly.
    // Compute both sampling and eviction estimates, prefer whichever is larger.
    const float evictionScale = 1.1f + (rollingEvictionAvg12h / knownNodeCount);
    const float samplingScale =
        (sampledEstimate > 0) ? std::max(static_cast<float>(sampledEstimate) / knownNodeCount, 1.0f) : 0.0f;

    if (samplingScale > evictionScale) {
        statusMode = STATUS_SCALED_FROM_MESH_ESTIMATE;
        scale = samplingScale;
        LOG_DEBUG("[HOPSCALE] scaleFactor=%.2f (sampled: est=%u > eviction=%.2f)", static_cast<double>(scale), sampledEstimate,
                  static_cast<double>(evictionScale));
    } else {
        statusMode = STATUS_FALLBACK_EVICTION_SCALE;
        scale = evictionScale;
        LOG_DEBUG("[HOPSCALE] scaleFactor=%.2f (eviction: evict/h=%.1f, sampled=%u scale=%.2f)", static_cast<double>(scale),
                  static_cast<double>(rollingEvictionAvg12h), sampledEstimate, static_cast<double>(samplingScale));
    }
    return scale;
}

bool HopScalingModule::checkStableStatus(const Snapshot &snapshot) const
{
    // Require at least a minimal amount of temporal spread before classifying status mode as stable.
    const uint32_t recentActiveNodes = snapshot.recent1h.total + snapshot.old1hFrom2h.total;
    const uint32_t olderActiveNodes = snapshot.old1hFrom2h.total + snapshot.old1hFrom3h.total;
    return snapshot.cumulative12h.total > 1 && recentActiveNodes > 1 && olderActiveNodes > 1;
}

void HopScalingModule::logStatusReport(const Snapshot &snapshot, bool didHourlyUpdate) const
{
    static const char *const modeNames[] = {"STARTUP", "STABLE", "EVICTION", "SAMPLED", "FALLBACK"};
    const char *modeName = (lastStatusMode < sizeof(modeNames) / sizeof(modeNames[0])) ? modeNames[lastStatusMode] : "?";
    LOG_INFO("[HOPSCALE] status mode=%s hourly=%u hop=%u actWt=%.2f polite=%.2f scale=%.2f evict/h=%.1f sampledEst=%u sampling 1 "
             "in %u n1=%u n2=%u n3=%u n12=%u",
             modeName, didHourlyUpdate ? 1 : 0, lastRequiredHop, static_cast<double>(lastActivityWeight),
             static_cast<double>(lastPolitenessFactor), static_cast<double>(lastScaleFactor),
             static_cast<double>(rollingEvictionAvg12h), lastSampledEstimate, SAMPLING_DENOMINATOR, snapshot.recent1h.total,
             snapshot.recent1h.total + snapshot.old1hFrom2h.total,
             snapshot.recent1h.total + snapshot.old1hFrom2h.total + snapshot.old1hFrom3h.total, snapshot.cumulative12h.total);
}

// Compute the required hop distance to reach the target number of affected nodes, applying scaling and politeness adjustments.
// The base hop is the minimum hop at which the cumulative affected nodes meets or exceeds the target. If extending to the next
// hop would still keep the total under the politeness limit, extend by one more hop to be more generous. Scaling
uint8_t HopScalingModule::computeRequiredHop(const Snapshot &snapshot, float scaleFactor, float politenessFactor) const
{
    uint16_t affectedNodesPerHop[HOP_MAX + 1];
    memset(affectedNodesPerHop, 0, sizeof(affectedNodesPerHop));

    // Linear extrapolation: scaleFactor is a per-hour additive turnover fraction,
    // so over 12 hours the total scale is 1 + 12*(scaleFactor - 1), not scaleFactor^12.
    const float scale12h = std::max(1.0f + 12.0f * (scaleFactor - 1.0f), 1.0f);

    for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
        // 12h cumulative is the best population estimate; scale12h compensates for eviction undercount or megamesh that
        // exceeds NodeDB capacity using linear extrapolation over the 12h window.
        const float scaled12h = snapshot.cumulative12h.perHop[hop] * scale12h;
        affectedNodesPerHop[hop] = static_cast<uint16_t>(std::min(scaled12h, static_cast<float>(UINT16_MAX)));
    }
    LOG_INFO("[HOPSCALE] perHop 1h:     [%u %u %u %u %u %u %u %u]", snapshot.recent1h.perHop[0], snapshot.recent1h.perHop[1],
             snapshot.recent1h.perHop[2], snapshot.recent1h.perHop[3], snapshot.recent1h.perHop[4], snapshot.recent1h.perHop[5],
             snapshot.recent1h.perHop[6], snapshot.recent1h.perHop[7]);
    LOG_INFO("[HOPSCALE] perHop 12h:    [%u %u %u %u %u %u %u %u]", snapshot.cumulative12h.perHop[0],
             snapshot.cumulative12h.perHop[1], snapshot.cumulative12h.perHop[2], snapshot.cumulative12h.perHop[3],
             snapshot.cumulative12h.perHop[4], snapshot.cumulative12h.perHop[5], snapshot.cumulative12h.perHop[6],
             snapshot.cumulative12h.perHop[7]);
    LOG_INFO("[HOPSCALE] perHop scaled: [%u %u %u %u %u %u %u %u]", affectedNodesPerHop[0], affectedNodesPerHop[1],
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
        const float politeLimit = TARGET_AFFECTED_NODES * politenessFactor;
        if (affectedIfExtend <= politeLimit) {
            baseHop++;
        }
    }

    return baseHop;
}

// Main scheduled task: run every 10 minutes for status reporting,
// and perform full recomputation once per hour.
int32_t HopScalingModule::runOnce()
{
    const bool isFirstRun = !this->hasCompletedInitialRun;
    bool didHourlyUpdate = false;

    if (isFirstRun) {
        this->hasCompletedInitialRun = true;
        this->runsSinceLastHourlyUpdate = 0;
        didHourlyUpdate = true;
    } else {
        this->runsSinceLastHourlyUpdate++;
        if (this->runsSinceLastHourlyUpdate >= RUNS_PER_HOUR) {
            this->runsSinceLastHourlyUpdate = 0;
            didHourlyUpdate = true;
        }
    }

    // The first run happens shortly after boot and should not roll the hourly bucket.
    if (didHourlyUpdate && !isFirstRun) {
        rollHour();
    }

    Snapshot snapshot{};
    buildSnapshot(snapshot);

    if (didHourlyUpdate) {
        lastActivityWeight = computeActivityWeight(snapshot);
        lastPolitenessFactor = selectPolitenessFactor(lastActivityWeight);
        lastScaleFactor = estimateScaleFactor(snapshot, lastStatusMode, lastSampledEstimate);
        if (!checkStableStatus(snapshot)) {
            lastStatusMode = STATUS_STARTUP_NOT_ENOUGH_DATA;
        }
        lastRequiredHop = computeRequiredHop(snapshot, lastScaleFactor, lastPolitenessFactor);
    }

    // Shared status report path for both periodic and hourly updates.
    logStatusReport(snapshot, didHourlyUpdate);

    return RUN_INTERVAL_MS;
}

#endif
