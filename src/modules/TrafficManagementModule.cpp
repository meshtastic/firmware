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
    TM_LOG_INFO("Allocating unified cache: %u entries (%u bytes, %s mode)", allocSize, allocSize * sizeof(UnifiedCacheEntry),
                useCompactTimestamps ? "compact" : "full");

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    // ESP32 with PSRAM: prefer PSRAM for large allocations (20 bytes/entry)
    cache = static_cast<UnifiedCacheEntry *>(ps_calloc(allocSize, sizeof(UnifiedCacheEntry)));
    if (!cache) {
        TM_LOG_WARN("PSRAM allocation failed, falling back to heap");
        cache = new UnifiedCacheEntry[allocSize]();
    }
#elif defined(ARCH_PORTDUINO)
    // Native/Portduino: heap allocation with full 20-byte structure (abundant memory)
    cache = new UnifiedCacheEntry[allocSize]();
#else
    // Memory-constrained boards (NRF52, non-PSRAM ESP32): compact 12-byte entries
    // Memory savings: 40% reduction vs full structure (12KB vs 20KB at 1024 entries)
    cache = new UnifiedCacheEntry[allocSize]();
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
        // For eviction, we need to compare across all time fields
#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
        // Full mode: pos_time_secs is 16-bit seconds
        uint16_t entryTime = cache[i].pos_time_secs;
#else
        // Compact mode: pos_time_mins is 8-bit minutes, scale to ~seconds for comparison
        // Multiply by 60 to get rough second equivalent (won't overflow uint16_t)
        uint16_t entryTime = static_cast<uint16_t>(cache[i].pos_time_mins) * 60;
#endif
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

// =============================================================================
// Epoch Management
// =============================================================================

/**
 * Reset the timestamp epoch when relative offsets approach overflow.
 *
 * This invalidates all cached timestamps, effectively clearing time-based
 * state while preserving node associations. Called during periodic maintenance:
 * - Full mode (PSRAM): when epoch age exceeds ~17 hours (16-bit seconds)
 * - Compact mode: when epoch age exceeds ~3.5 hours (8-bit minutes for position)
 */
void TrafficManagementModule::resetEpoch(uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    TM_LOG_DEBUG("Resetting cache epoch (%s mode)", useCompactTimestamps ? "compact" : "full");
    cacheEpochMs = nowMs;

    // Invalidate all relative timestamps
    for (uint16_t i = 0; i < cacheSize(); i++) {
#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
        // Full mode: clear 16-bit second timestamps
        cache[i].pos_time_secs = 0;
#else
        // Compact mode: clear 8-bit minute timestamp
        cache[i].pos_time_mins = 0;
#endif
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
// Position Hash (Compact Mode)
// =============================================================================

/**
 * Compute an 8-bit hash from truncated lat/lon coordinates.
 *
 * This hash is used on memory-constrained platforms (NRF52, non-PSRAM ESP32)
 * instead of storing full 32-bit coordinates. The hash saves 7 bytes per cache
 * entry (8 bytes for two int32_t vs 1 byte for hash).
 *
 * Hash quality considerations:
 * - Uses golden ratio multiplication (2654435761) for good bit mixing
 * - XOR combines lat/lon to spread coordinate changes across all bits
 * - Final fold ensures all 32 bits contribute to the 8-bit result
 *
 * Collision probability: ~0.4% (1/256) for random positions
 * Impact of collision: May drop a valid position update (acceptable trade-off)
 *
 * @param lat_truncated  Precision-truncated latitude
 * @param lon_truncated  Precision-truncated longitude
 * @return               8-bit hash suitable for deduplication
 */
uint8_t TrafficManagementModule::computePositionHash(int32_t lat_truncated, int32_t lon_truncated)
{
    // Combine coordinates using golden ratio hash for good distribution
    // 2654435761 is the closest prime to 2^32 / golden_ratio
    uint32_t h = static_cast<uint32_t>(lat_truncated) ^ (static_cast<uint32_t>(lon_truncated) * 2654435761u);

    // Fold 32 bits down to 8 bits, ensuring all bits contribute
    h ^= (h >> 16);
    h ^= (h >> 8);

    return static_cast<uint8_t>(h);
}

// =============================================================================
// Packet Handling
// =============================================================================

ProcessMessage TrafficManagementModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return ProcessMessage::CONTINUE;

    ignoreRequest = false;
    exhaustRequested = false; // Reset exhaust flag for this packet
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

    // Skip our own packets - only zero-hop relayed packets from other nodes
    if (isFromUs(&mp))
        return;

    // -------------------------------------------------------------------------
    // Relayed Broadcast Hop Exhaustion
    // -------------------------------------------------------------------------
    // For relayed telemetry or position broadcasts from other nodes, optionally
    // set hop_limit=0 so they don't propagate further through the mesh.
    // Our own packets are unaffected and will use normal hop_limit.

    const auto &cfg = moduleConfig.traffic_management;
    const bool isTelemetry = mp.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    const bool isPosition = mp.decoded.portnum == meshtastic_PortNum_POSITION_APP;
    const bool shouldExhaust = (isTelemetry && cfg.exhaust_hop_telemetry) || (isPosition && cfg.exhaust_hop_position);

    TM_LOG_DEBUG(
        "alterReceived: from=0x%08x port=%d isTelemetry=%d isPosition=%d zeroHopTelem=%d exhaustHopPos=%d shouldExhaust=%d "
        "isBroadcast=%d hop_limit=%d",
        getFrom(&mp), mp.decoded.portnum, isTelemetry, isPosition, cfg.exhaust_hop_telemetry, cfg.exhaust_hop_position,
        shouldExhaust, isBroadcast(mp.to), mp.hop_limit);

    if (!shouldExhaust || !isBroadcast(mp.to))
        return;

    if (cfg.dry_run) {
        logAction("exhaust", &mp, "dry-run");
        incrementStat(&stats.dry_run_would_drop);
        return;
    }

    if (mp.hop_limit > 0) {
        const char *reason = isTelemetry ? "zero-hop-telemetry" : "exhaust-hop-position";
        logAction("exhaust", &mp, reason);
        exhaustHops(&mp);
        exhaustRequested = true; // Signal to perhapsRebroadcast() to force hop_limit = 0
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

    // Check if epoch reset needed
    // - Full mode (PSRAM): ~17 hours (approaching 16-bit second overflow)
    // - Compact mode: ~3.5 hours (approaching 8-bit minute overflow for position)
    if (needsEpochReset(nowMs)) {
        concurrency::LockGuard guard(&cacheLock);
        resetEpoch(nowMs);
        return kMaintenanceIntervalMs;
    }

    // Calculate TTLs for cache expiration
    // Position TTL: 4x the configured interval, or default (~10 min)
    const uint32_t positionIntervalMs = secsToMs(moduleConfig.traffic_management.position_min_interval_secs);
    const uint64_t positionTtlCalc =
        (positionIntervalMs > 0) ? static_cast<uint64_t>(positionIntervalMs) * 4ULL : kDefaultCacheTtlMs;
    const uint32_t positionTtlMs = positionTtlCalc > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(positionTtlCalc);

    // Rate limit TTL: 2x the window, or default
    const uint32_t rateIntervalMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);
    const uint64_t rateTtlCalc = (rateIntervalMs > 0) ? static_cast<uint64_t>(rateIntervalMs) * 2ULL : kDefaultCacheTtlMs;
    const uint32_t rateTtlMs = rateTtlCalc > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rateTtlCalc);

    // Unknown packet TTL: 5x the reset window
    const uint32_t unknownTtlMs = kUnknownResetMs * 5;

    // Sweep cache and clear expired entries
    concurrency::LockGuard guard(&cacheLock);
    for (uint16_t i = 0; i < cacheSize(); i++) {
        if (cache[i].node == 0)
            continue;

        bool anyValid = false;

        // ---------------------------------------------------------------------
        // Check and clear expired position data
        // ---------------------------------------------------------------------
#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
        // Full mode: 16-bit second timestamps, stores actual coordinates
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
#else
        // Compact mode: 8-bit minute timestamps, stores position hash
        if (cache[i].pos_time_mins != 0) {
            uint32_t posTimeMs = fromRelativeMins(cache[i].pos_time_mins);
            if (!isWithinWindow(nowMs, posTimeMs, positionTtlMs)) {
                cache[i].pos_hash = 0;
                cache[i].pos_time_mins = 0;
            } else {
                anyValid = true;
            }
        }
#endif

        // ---------------------------------------------------------------------
        // Check and clear expired rate limit data (same for both modes)
        // ---------------------------------------------------------------------
        if (cache[i].rate_window_secs != 0) {
            uint32_t rateTimeMs = fromRelativeSecs(cache[i].rate_window_secs);
            if (!isWithinWindow(nowMs, rateTimeMs, rateTtlMs)) {
                cache[i].rate_count = 0;
                cache[i].rate_window_secs = 0;
            } else {
                anyValid = true;
            }
        }

        // ---------------------------------------------------------------------
        // Check and clear expired unknown tracking data (same for both modes)
        // ---------------------------------------------------------------------
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

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
    // -------------------------------------------------------------------------
    // Full mode (PSRAM): Compare actual truncated coordinates
    // -------------------------------------------------------------------------
    // Stores complete lat/lon for precise duplicate detection.
    // Uses 16-bit second timestamps (~18 hour range).
    //
    const bool same = !isNew && entry->lat_truncated == lat_truncated && entry->lon_truncated == lon_truncated;
    const bool within =
        (minIntervalMs == 0) ? true : isWithinWindow(nowMs, fromRelativeSecs(entry->pos_time_secs), minIntervalMs);

    entry->lat_truncated = lat_truncated;
    entry->lon_truncated = lon_truncated;
    entry->pos_time_secs = toRelativeSecs(nowMs);
#else
    // -------------------------------------------------------------------------
    // Compact mode (NRF52, non-PSRAM ESP32): Compare 8-bit position hash
    // -------------------------------------------------------------------------
    // Saves 7 bytes per entry by hashing coordinates instead of storing them.
    // Hash collision (~0.4%) may occasionally drop a valid position - acceptable.
    // Uses 8-bit minute timestamps (~4 hour range, default for position dedup).
    //
    const uint8_t posHash = computePositionHash(lat_truncated, lon_truncated);
    const bool same = !isNew && entry->pos_hash == posHash;
    const bool within =
        (minIntervalMs == 0) ? true : isWithinWindow(nowMs, fromRelativeMins(entry->pos_time_mins), minIntervalMs);

    entry->pos_hash = posHash;
    entry->pos_time_mins = toRelativeMins(nowMs);
#endif

    return same && within;
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
    // Set to 0 to ensure the packet won't propagate further after this node relays it.
    // The exhaustRequested flag signals perhapsRebroadcast() to preserve hop_limit=0
    // even if router_preserve_hops or favorite node logic would normally prevent decrement.
    p->hop_limit = 0;
}

void TrafficManagementModule::logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const
{
    int portnum = -1;
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        portnum = p->decoded.portnum;
    }
    TM_LOG_INFO("%s from=0x%08x to=0x%08x port=%d reason=%s", action, getFrom(p), p->to, portnum, reason);
}

#endif
