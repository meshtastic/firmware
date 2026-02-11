#include "TrafficManagementModule.h"

#if HAS_TRAFFIC_MANAGEMENT

#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "TypeConversions.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>
#include <cstring>

#define TM_LOG_DEBUG(fmt, ...) LOG_DEBUG("[TM] " fmt, ##__VA_ARGS__)
#define TM_LOG_INFO(fmt, ...) LOG_INFO("[TM] " fmt, ##__VA_ARGS__)
#define TM_LOG_WARN(fmt, ...) LOG_WARN("[TM] " fmt, ##__VA_ARGS__)

// =============================================================================
// Anonymous Namespace - Internal Helpers
// =============================================================================

namespace
{

constexpr uint32_t kMaintenanceIntervalMs = 60 * 1000UL;  // Cache cleanup interval
constexpr uint32_t kUnknownResetMs = 60 * 1000UL;         // Unknown packet window
constexpr uint32_t kDefaultCacheTtlMs = 10 * 60 * 1000UL; // Default TTL for cache entries
constexpr uint8_t kMaxCuckooKicks = 16;                   // Max displacement chain length

/**
 * Convert seconds to milliseconds with overflow protection.
 */
uint32_t secsToMs(uint32_t secs)
{
    uint64_t ms = static_cast<uint64_t>(secs) * 1000ULL;
    if (ms > UINT32_MAX)
        return UINT32_MAX;
    return static_cast<uint32_t>(ms);
}

/**
 * Check if a timestamp is within a time window.
 * Handles wrap-around correctly using unsigned subtraction.
 */
bool isWithinWindow(uint32_t nowMs, uint32_t startMs, uint32_t intervalMs)
{
    if (intervalMs == 0 || startMs == 0)
        return false;
    return (nowMs - startMs) < intervalMs;
}

/**
 * Truncate lat/lon to specified precision for position deduplication.
 *
 * The truncation works by masking off lower bits and rounding to the center
 * of the resulting grid cell. This creates a stable truncated value even
 * when GPS jitter causes small coordinate changes.
 *
 * @param value     Raw latitude_i or longitude_i from position
 * @param precision Number of significant bits to keep (0-32)
 * @return          Truncated and centered coordinate value
 */
int32_t truncateLatLon(int32_t value, uint8_t precision)
{
    if (precision == 0 || precision >= 32)
        return value;

    // Create mask to zero out lower bits
    uint32_t mask = UINT32_MAX << (32 - precision);
    uint32_t truncated = static_cast<uint32_t>(value) & mask;

    // Add half the truncation step to center in the grid cell
    truncated += (1u << (31 - precision));
    return static_cast<int32_t>(truncated);
}

/**
 * Saturating increment for uint8_t counters.
 * Prevents overflow by capping at UINT8_MAX (255).
 */
inline void saturatingIncrement(uint8_t &counter)
{
    if (counter < UINT8_MAX)
        counter++;
}

} // namespace

// =============================================================================
// Module Instance
// =============================================================================

TrafficManagementModule *trafficManagementModule;

// =============================================================================
// Constructor
// =============================================================================

TrafficManagementModule::TrafficManagementModule() : MeshModule("TrafficManagement"), concurrency::OSThread("TrafficManagement")
{
    // Module configuration
    isPromiscuous = true; // See all packets, not just those addressed to us
    encryptedOk = true;   // Can process encrypted packets
    loopbackOk = true;    // Process our own outgoing packets (for hop exhaustion)
    stats = meshtastic_TrafficManagementStats_init_zero;

    // Initialize rolling epoch for relative timestamps
    cacheEpochMs = millis();

// Allocate unified cache using cuckoo hash table sizing
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    const uint16_t allocSize = cacheSize();
    TM_LOG_INFO("Allocating unified cache: %u entries (%u bytes)", allocSize, allocSize * sizeof(UnifiedCacheEntry));

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    // PSRAM-equipped boards: prefer PSRAM for large allocations
    cache = static_cast<UnifiedCacheEntry *>(ps_calloc(allocSize, sizeof(UnifiedCacheEntry)));
#if defined(ARCH_NRF52)
    hashCache = static_cast<HashDedupEntry *>(ps_calloc(allocSize, sizeof(HashDedupEntry)));
#endif
    if (!cache
#if defined(ARCH_NRF52)
        || !hashCache
#endif
    ) {
        TM_LOG_WARN("PSRAM allocation failed, falling back to heap");
        if (!cache)
            cache = new UnifiedCacheEntry[allocSize]();
#if defined(ARCH_NRF52)
        if (!hashCache)
            hashCache = new HashDedupEntry[allocSize]();
#endif
    }
#else
    // Non-PSRAM: direct heap allocation with zero-initialization
    cache = new UnifiedCacheEntry[allocSize]();
#if defined(ARCH_NRF52)
    hashCache = new HashDedupEntry[allocSize]();
#endif
#endif

#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE > 0

    setIntervalFromNow(kMaintenanceIntervalMs);
}

// =============================================================================
// Statistics
// =============================================================================

meshtastic_TrafficManagementStats TrafficManagementModule::getStats() const
{
    concurrency::LockGuard guard(&cacheLock);
    return stats;
}

void TrafficManagementModule::resetStats()
{
    concurrency::LockGuard guard(&cacheLock);
    stats = meshtastic_TrafficManagementStats_init_zero;
}

void TrafficManagementModule::recordRouterHopPreserved()
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return;
    incrementStat(&stats.router_hops_preserved);
}

void TrafficManagementModule::incrementStat(uint32_t *field)
{
    concurrency::LockGuard guard(&cacheLock);
    (*field)++;
}

// =============================================================================
// Cuckoo Hash Table Operations
// =============================================================================

/**
 * Find an existing entry for the given node.
 *
 * Cuckoo hashing guarantees that if an entry exists, it's in one of exactly
 * two locations: hash1(node) or hash2(node). This provides O(1) lookup.
 *
 * @param node NodeNum to search for
 * @return     Pointer to entry if found, nullptr otherwise
 */
TrafficManagementModule::UnifiedCacheEntry *TrafficManagementModule::findEntry(NodeNum node)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)node;
    return nullptr;
#else
    if (!cache || node == 0)
        return nullptr;

    // Check primary location
    uint16_t h1 = cuckooHash1(node);
    if (cache[h1].node == node)
        return &cache[h1];

    // Check alternate location
    uint16_t h2 = cuckooHash2(node);
    if (cache[h2].node == node)
        return &cache[h2];

    return nullptr;
#endif
}

/**
 * Find or create an entry for the given node using cuckoo hashing.
 *
 * If the node exists, returns the existing entry. Otherwise, attempts to
 * insert a new entry using cuckoo displacement:
 *
 * 1. Try to insert at h1(node) - if empty, done
 * 2. Try to insert at h2(node) - if empty, done
 * 3. Kick existing entry from h1 to its alternate location
 * 4. Repeat up to kMaxCuckooKicks times
 * 5. If cycle detected or max kicks exceeded, evict oldest entry
 *
 * @param node  NodeNum to find or create
 * @param isNew Set to true if a new entry was created
 * @return      Pointer to entry, or nullptr if allocation failed
 */
TrafficManagementModule::UnifiedCacheEntry *TrafficManagementModule::findOrCreateEntry(NodeNum node, bool *isNew)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)node;
    if (isNew)
        *isNew = false;
    return nullptr;
#else
    if (!cache || node == 0) {
        if (isNew)
            *isNew = false;
        return nullptr;
    }

    // Check if entry already exists (O(1) lookup)
    uint16_t h1 = cuckooHash1(node);
    if (cache[h1].node == node) {
        if (isNew)
            *isNew = false;
        return &cache[h1];
    }

    uint16_t h2 = cuckooHash2(node);
    if (cache[h2].node == node) {
        if (isNew)
            *isNew = false;
        return &cache[h2];
    }

    // Entry doesn't exist - try to insert

    // Prefer empty slot at h1
    if (cache[h1].node == 0) {
        memset(&cache[h1], 0, sizeof(UnifiedCacheEntry));
        cache[h1].node = node;
        if (isNew)
            *isNew = true;
        return &cache[h1];
    }

    // Try empty slot at h2
    if (cache[h2].node == 0) {
        memset(&cache[h2], 0, sizeof(UnifiedCacheEntry));
        cache[h2].node = node;
        if (isNew)
            *isNew = true;
        return &cache[h2];
    }

    // Both slots occupied - perform cuckoo displacement
    // Start by kicking entry at h1 to its alternate location
    UnifiedCacheEntry displaced = cache[h1];
    memset(&cache[h1], 0, sizeof(UnifiedCacheEntry));
    cache[h1].node = node;

    for (uint8_t kicks = 0; kicks < kMaxCuckooKicks; kicks++) {
        // Find alternate location for displaced entry
        uint16_t altH1 = cuckooHash1(displaced.node);
        uint16_t altH2 = cuckooHash2(displaced.node);
        uint16_t altSlot = (altH1 == h1) ? altH2 : altH1;

        if (cache[altSlot].node == 0) {
            // Found empty slot - insert displaced entry
            cache[altSlot] = displaced;
            if (isNew)
                *isNew = true;
            return &cache[h1];
        }

        // Kick entry from alternate slot
        UnifiedCacheEntry temp = cache[altSlot];
        cache[altSlot] = displaced;
        displaced = temp;
        h1 = altSlot;
    }

    // Cuckoo cycle detected or max kicks exceeded
    // Fall back to evicting oldest entry globally
    TM_LOG_DEBUG("Cuckoo cycle detected, evicting oldest entry");

    uint16_t oldestIdx = 0;
    uint16_t oldestTime = UINT16_MAX;

    for (uint16_t i = 0; i < cacheSize(); i++) {
        // Use max of all timestamp fields as "last activity"
        uint16_t entryTime = cache[i].pos_time_secs;
        if (cache[i].rate_window_secs > entryTime)
            entryTime = cache[i].rate_window_secs;
        if (cache[i].unknown_secs > entryTime)
            entryTime = cache[i].unknown_secs;

        if (entryTime < oldestTime) {
            oldestTime = entryTime;
            oldestIdx = i;
        }
    }

    // Place displaced entry in oldest slot
    cache[oldestIdx] = displaced;

    if (isNew)
        *isNew = true;
    return &cache[cuckooHash1(node)]; // Return slot for original node
#endif
}

#if defined(ARCH_NRF52)
/**
 * Find or create hash entry for NRF52 position deduplication.
 * Uses same cuckoo hashing approach but keyed on content hash instead of NodeNum.
 */
TrafficManagementModule::HashDedupEntry *TrafficManagementModule::findHashEntry(uint32_t hash, bool *isNew)
{
    if (!hashCache) {
        if (isNew)
            *isNew = false;
        return nullptr;
    }

    uint16_t h1 = hash & cacheMask();
    uint16_t h2 = ((hash * 2654435769u) >> (32 - cuckooHashBits())) & cacheMask();

    // Check if entry exists
    if (hashCache[h1].hash == hash && hashCache[h1].last_seen_ms != 0) {
        if (isNew)
            *isNew = false;
        return &hashCache[h1];
    }
    if (hashCache[h2].hash == hash && hashCache[h2].last_seen_ms != 0) {
        if (isNew)
            *isNew = false;
        return &hashCache[h2];
    }

    // Insert new entry
    uint16_t slot = (hashCache[h1].last_seen_ms == 0)                           ? h1
                    : (hashCache[h2].last_seen_ms == 0)                         ? h2
                    : (hashCache[h1].last_seen_ms < hashCache[h2].last_seen_ms) ? h1
                                                                                : h2;

    memset(&hashCache[slot], 0, sizeof(HashDedupEntry));
    hashCache[slot].hash = hash;
    if (isNew)
        *isNew = true;
    return &hashCache[slot];
}
#endif

// =============================================================================
// Epoch Management
// =============================================================================

/**
 * Reset the timestamp epoch when relative offsets approach overflow.
 *
 * This invalidates all cached timestamps, effectively clearing time-based
 * state while preserving node associations. Called during periodic maintenance
 * when epoch age exceeds ~17 hours.
 */
void TrafficManagementModule::resetEpoch(uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    TM_LOG_DEBUG("Resetting cache epoch");
    cacheEpochMs = nowMs;

    // Invalidate all relative timestamps
    for (uint16_t i = 0; i < cacheSize(); i++) {
        cache[i].pos_time_secs = 0;
        cache[i].rate_window_secs = 0;
        cache[i].unknown_secs = 0;
        cache[i].rate_count = 0;
        cache[i].unknown_count = 0;
    }
#else
    (void)nowMs;
#endif
}

// =============================================================================
// Packet Handling
// =============================================================================

ProcessMessage TrafficManagementModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return ProcessMessage::CONTINUE;

    ignoreRequest = false;
    incrementStat(&stats.packets_inspected);

    const auto &cfg = moduleConfig.traffic_management;
    const bool dryRun = cfg.dry_run;
    const uint32_t nowMs = millis();

    // -------------------------------------------------------------------------
    // Undecoded Packet Handling
    // -------------------------------------------------------------------------
    // Packets we can't decode (wrong key, corruption, etc.) may indicate
    // a misbehaving node. Track and optionally drop repeat offenders.

    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        if (cfg.drop_unknown_enabled && cfg.unknown_packet_threshold > 0) {
            if (shouldDropUnknown(&mp, nowMs)) {
                logAction("drop", &mp, "unknown");
                if (dryRun) {
                    incrementStat(&stats.dry_run_would_drop);
                    return ProcessMessage::CONTINUE;
                }
                incrementStat(&stats.unknown_packet_drops);
                ignoreRequest = true;
                return ProcessMessage::STOP;
            }
        }
        return ProcessMessage::CONTINUE;
    }

    // -------------------------------------------------------------------------
    // NodeInfo Direct Response
    // -------------------------------------------------------------------------
    // When we see a unicast NodeInfo request for a node we know about,
    // respond directly from cache instead of forwarding the request.
    // This reduces mesh traffic for common "who is node X?" queries.

    if (cfg.nodeinfo_direct_response && mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP && mp.decoded.want_response &&
        !isBroadcast(mp.to) && !isToUs(&mp) && !isFromUs(&mp)) {
        if (shouldRespondToNodeInfo(&mp, !dryRun)) {
            logAction("respond", &mp, "nodeinfo-cache");
            if (dryRun) {
                incrementStat(&stats.dry_run_would_drop);
                return ProcessMessage::CONTINUE;
            }
            incrementStat(&stats.nodeinfo_cache_hits);
            ignoreRequest = true;
            return ProcessMessage::STOP;
        }
    }

    // -------------------------------------------------------------------------
    // Position Deduplication
    // -------------------------------------------------------------------------
    // Drop position broadcasts that haven't moved significantly since the
    // last broadcast from this node. Uses truncated coordinates to ignore
    // GPS jitter within the configured precision.

    if (!isFromUs(&mp)) {
        if (cfg.position_dedup_enabled && mp.decoded.portnum == meshtastic_PortNum_POSITION_APP) {
            meshtastic_Position pos = meshtastic_Position_init_zero;
            if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &pos)) {
                if (shouldDropPosition(&mp, &pos, nowMs)) {
                    logAction("drop", &mp, "position-dedup");
                    if (dryRun) {
                        incrementStat(&stats.dry_run_would_drop);
                        return ProcessMessage::CONTINUE;
                    }
                    incrementStat(&stats.position_dedup_drops);
                    ignoreRequest = true;
                    return ProcessMessage::STOP;
                }
            }
        }

        // ---------------------------------------------------------------------
        // Rate Limiting
        // ---------------------------------------------------------------------
        // Throttle nodes sending too many packets within a time window.
        // Excludes routing and admin packets which are essential for mesh operation.

        if (cfg.rate_limit_enabled && cfg.rate_limit_window_secs > 0 && cfg.rate_limit_max_packets > 0) {
            if (mp.decoded.portnum != meshtastic_PortNum_ROUTING_APP && mp.decoded.portnum != meshtastic_PortNum_ADMIN_APP) {
                if (isRateLimited(mp.from, nowMs)) {
                    logAction("drop", &mp, "rate-limit");
                    if (dryRun) {
                        incrementStat(&stats.dry_run_would_drop);
                        return ProcessMessage::CONTINUE;
                    }
                    incrementStat(&stats.rate_limit_drops);
                    ignoreRequest = true;
                    return ProcessMessage::STOP;
                }
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

void TrafficManagementModule::alterReceived(meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return;

    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return;

    if (!isFromUs(&mp))
        return;

    // -------------------------------------------------------------------------
    // Local-Only Broadcast Hop Exhaustion
    // -------------------------------------------------------------------------
    // For locally-originated broadcasts of telemetry or position, optionally
    // set hop_limit=0 so the packet only reaches direct neighbors.
    // Useful for high-frequency sensor data that doesn't need mesh-wide distribution.

    const auto &cfg = moduleConfig.traffic_management;
    const bool isTelemetry = mp.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    const bool isPosition = mp.decoded.portnum == meshtastic_PortNum_POSITION_APP;
    const bool shouldExhaust = (isTelemetry && cfg.local_only_telemetry) || (isPosition && cfg.local_only_position);

    if (!shouldExhaust || !isBroadcast(mp.to))
        return;

    if (cfg.dry_run) {
        logAction("exhaust", &mp, "dry-run");
        incrementStat(&stats.dry_run_would_drop);
        return;
    }

    if (mp.hop_limit > 0) {
        exhaustHops(&mp);
        incrementStat(&stats.hop_exhausted_packets);
    }
}

// =============================================================================
// Periodic Maintenance
// =============================================================================

int32_t TrafficManagementModule::runOnce()
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return INT32_MAX;

#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    const uint32_t nowMs = millis();

    // Check if epoch reset needed (approaching 16-bit overflow)
    if (needsEpochReset(nowMs)) {
        concurrency::LockGuard guard(&cacheLock);
        resetEpoch(nowMs);
        return kMaintenanceIntervalMs;
    }

    // Calculate TTLs for cache expiration
    const uint32_t positionIntervalMs = secsToMs(moduleConfig.traffic_management.position_min_interval_secs);
    const uint64_t positionTtlCalc =
        (positionIntervalMs > 0) ? static_cast<uint64_t>(positionIntervalMs) * 4ULL : kDefaultCacheTtlMs;
    const uint32_t positionTtlMs = positionTtlCalc > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(positionTtlCalc);

    const uint32_t rateIntervalMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);
    const uint64_t rateTtlCalc = (rateIntervalMs > 0) ? static_cast<uint64_t>(rateIntervalMs) * 2ULL : kDefaultCacheTtlMs;
    const uint32_t rateTtlMs = rateTtlCalc > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rateTtlCalc);
    const uint32_t unknownTtlMs = kUnknownResetMs * 5;

    // Sweep cache and clear expired entries
    concurrency::LockGuard guard(&cacheLock);
    for (uint16_t i = 0; i < cacheSize(); i++) {
        if (cache[i].node == 0)
            continue;

        bool anyValid = false;

        // Check and clear expired position data
        if (cache[i].pos_time_secs != 0) {
            uint32_t posTimeMs = fromRelativeSecs(cache[i].pos_time_secs);
            if (!isWithinWindow(nowMs, posTimeMs, positionTtlMs)) {
                cache[i].lat_truncated = 0;
                cache[i].lon_truncated = 0;
                cache[i].pos_time_secs = 0;
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired rate limit data
        if (cache[i].rate_window_secs != 0) {
            uint32_t rateTimeMs = fromRelativeSecs(cache[i].rate_window_secs);
            if (!isWithinWindow(nowMs, rateTimeMs, rateTtlMs)) {
                cache[i].rate_count = 0;
                cache[i].rate_window_secs = 0;
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired unknown tracking data
        if (cache[i].unknown_secs != 0) {
            uint32_t unknownTimeMs = fromRelativeSecs(cache[i].unknown_secs);
            if (!isWithinWindow(nowMs, unknownTimeMs, unknownTtlMs)) {
                cache[i].unknown_count = 0;
                cache[i].unknown_secs = 0;
            } else {
                anyValid = true;
            }
        }

        // If all data expired, free the slot entirely
        if (!anyValid) {
            memset(&cache[i], 0, sizeof(UnifiedCacheEntry));
        }
    }

#if defined(ARCH_NRF52)
    // Clean up hash cache for NRF52
    if (hashCache) {
        for (uint16_t i = 0; i < cacheSize(); i++) {
            if (hashCache[i].last_seen_ms != 0 && !isWithinWindow(nowMs, hashCache[i].last_seen_ms, positionTtlMs)) {
                memset(&hashCache[i], 0, sizeof(HashDedupEntry));
            }
        }
    }
#endif

#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE > 0

    return kMaintenanceIntervalMs;
}

// =============================================================================
// Traffic Management Logic
// =============================================================================

bool TrafficManagementModule::shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)p;
    (void)pos;
    (void)nowMs;
    return false;
#else
    if (!pos->has_latitude_i || !pos->has_longitude_i)
        return false;

    uint8_t precision = moduleConfig.traffic_management.position_precision_bits;
    if (precision > 32)
        precision = 32;

    const int32_t lat_truncated = truncateLatLon(pos->latitude_i, precision);
    const int32_t lon_truncated = truncateLatLon(pos->longitude_i, precision);
    const uint32_t minIntervalMs = secsToMs(moduleConfig.traffic_management.position_min_interval_secs);

#if defined(ARCH_NRF52)
    // NRF52: Use hash-based deduplication to save memory
    const uint32_t hash = computePacketHash(p, precision);
    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    HashDedupEntry *entry = findHashEntry(hash, &isNew);
    if (!entry)
        return false;

    const bool same = !isNew && entry->hash == hash;
    const bool within = (minIntervalMs == 0) ? true : isWithinWindow(nowMs, entry->last_seen_ms, minIntervalMs);

    entry->hash = hash;
    entry->last_seen_ms = nowMs;

    return same && within;
#else
    // Other platforms: Use unified cache with per-node coordinates
    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    const bool same = !isNew && entry->lat_truncated == lat_truncated && entry->lon_truncated == lon_truncated;
    const bool within =
        (minIntervalMs == 0) ? true : isWithinWindow(nowMs, fromRelativeSecs(entry->pos_time_secs), minIntervalMs);

    entry->lat_truncated = lat_truncated;
    entry->lon_truncated = lon_truncated;
    entry->pos_time_secs = toRelativeSecs(nowMs);

    return same && within;
#endif
#endif
}

bool TrafficManagementModule::shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse)
{
    if (!moduleConfig.traffic_management.nodeinfo_direct_response)
        return false;

    if (isBroadcast(p->to) || isToUs(p) || isFromUs(p))
        return false;

    if (!p->decoded.want_response)
        return false;

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->to);
    if (!node || !node->has_user)
        return false;

    if (!isMinHopsFromRequestor(p))
        return false;

    if (!sendResponse)
        return true;

    meshtastic_User user = TypeConversions::ConvertToUser(node->num, node->user);
    meshtastic_MeshPacket *reply = router->allocForSending();
    if (!reply) {
        TM_LOG_WARN("NodeInfo direct response dropped: no packet buffer");
        return false;
    }

    reply->decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    reply->decoded.payload.size =
        pb_encode_to_bytes(reply->decoded.payload.bytes, sizeof(reply->decoded.payload.bytes), &meshtastic_User_msg, &user);
    reply->decoded.want_response = false;
    reply->from = p->to; // Spoof sender as the target node we're responding on behalf of
    reply->to = getFrom(p);
    reply->channel = p->channel;
    reply->decoded.request_id = p->id;
    reply->hop_limit = 0;
    reply->next_hop = nodeDB->getLastByteOfNodeNum(getFrom(p));
    reply->priority = meshtastic_MeshPacket_Priority_DEFAULT;

    service->sendToMesh(reply);
    return true;
}

bool TrafficManagementModule::isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const
{
    int8_t hopsAway = getHopsAway(*p, -1);
    if (hopsAway < 0)
        return false;

    uint32_t minHops = moduleConfig.traffic_management.nodeinfo_direct_response_min_hops;
    if (minHops == 0)
        minHops = 2;
    if (minHops > 7)
        minHops = 7;

    return static_cast<uint32_t>(hopsAway) >= minHops;
}

bool TrafficManagementModule::isRateLimited(NodeNum from, uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)from;
    (void)nowMs;
    return false;
#else
    const uint32_t windowMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);
    if (windowMs == 0 || moduleConfig.traffic_management.rate_limit_max_packets == 0)
        return false;

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(from, &isNew);
    if (!entry)
        return false;

    // Check if window has expired
    if (isNew || !isWithinWindow(nowMs, fromRelativeSecs(entry->rate_window_secs), windowMs)) {
        entry->rate_window_secs = toRelativeSecs(nowMs);
        entry->rate_count = 1;
        return false;
    }

    // Increment counter (saturates at 255)
    saturatingIncrement(entry->rate_count);

    // Check against threshold (uint8_t max is 255, but config is uint32_t)
    uint32_t threshold = moduleConfig.traffic_management.rate_limit_max_packets;
    if (threshold > 255)
        threshold = 255;

    return entry->rate_count > threshold;
#endif
}

bool TrafficManagementModule::shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)p;
    (void)nowMs;
    return false;
#else
    if (!moduleConfig.traffic_management.drop_unknown_enabled || moduleConfig.traffic_management.unknown_packet_threshold == 0)
        return false;

    uint32_t windowMs = kUnknownResetMs;
    if (moduleConfig.traffic_management.rate_limit_window_secs > 0)
        windowMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    // Check if window has expired
    if (isNew || !isWithinWindow(nowMs, fromRelativeSecs(entry->unknown_secs), windowMs)) {
        entry->unknown_secs = toRelativeSecs(nowMs);
        entry->unknown_count = 0;
    }

    // Increment counter (saturates at 255)
    saturatingIncrement(entry->unknown_count);

    // Check against threshold
    uint32_t threshold = moduleConfig.traffic_management.unknown_packet_threshold;
    if (threshold > 255)
        threshold = 255;

    return entry->unknown_count > threshold;
#endif
}

void TrafficManagementModule::exhaustHops(meshtastic_MeshPacket *p)
{
    p->hop_limit = 0;
}

#if defined(ARCH_NRF52)
/**
 * Compute a hash for position deduplication on memory-constrained NRF52.
 *
 * The hash combines:
 * - Node ID (sender)
 * - Port number (should be POSITION_APP)
 * - Truncated latitude and longitude
 * - A salt from the device's private key
 *
 * Using a content-based hash instead of storing per-node coordinates
 * saves 8 bytes per cache entry (no lat/lon fields needed).
 */
uint32_t TrafficManagementModule::computePacketHash(const meshtastic_MeshPacket *p, uint8_t precision) const
{
    // Initialize with salt from private key to prevent cross-device collisions
    uint32_t hash = 0xA5A5A5A5;
    if (config.security.private_key.size >= 3) {
        hash = config.security.private_key.bytes[0];
        hash ^= static_cast<uint32_t>(config.security.private_key.bytes[1]) << 8;
        hash ^= static_cast<uint32_t>(config.security.private_key.bytes[2]) << 16;
    }

    // Mix in sender and port
    hash ^= getFrom(p);
    hash = (hash * 31) + static_cast<uint32_t>(p->decoded.portnum);

    // Mix in truncated position if this is a position packet
    if (p->decoded.portnum == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position pos = meshtastic_Position_init_zero;
        if (pb_decode_from_bytes(p->decoded.payload.bytes, p->decoded.payload.size, &meshtastic_Position_msg, &pos)) {
            const int32_t lat_truncated = truncateLatLon(pos.latitude_i, precision);
            const int32_t lon_truncated = truncateLatLon(pos.longitude_i, precision);
            hash = (hash * 31) + static_cast<uint32_t>(lat_truncated);
            hash = (hash * 31) + static_cast<uint32_t>(lon_truncated);
        }
    }

    // Final mixing (MurmurHash3 finalizer)
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);

    return hash;
}
#endif

void TrafficManagementModule::logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const
{
    int portnum = -1;
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        portnum = p->decoded.portnum;
    }
    TM_LOG_INFO("%s from=0x%08x to=0x%08x port=%d reason=%s", action, getFrom(p), p->to, portnum, reason);
}

#endif
