#include "TrafficManagementModule.h"

#if HAS_TRAFFIC_MANAGEMENT

#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "TypeConversions.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
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

constexpr uint32_t kMaintenanceIntervalMs = 60 * 1000UL; // Cache cleanup interval
constexpr uint32_t kUnknownResetMs = 60 * 1000UL;        // Unknown packet window
constexpr uint8_t kMaxCuckooKicks = 16;                  // Max displacement chain length

// NodeInfo direct response: enforced maximum hops by device role
// Both use maxHops logic (respond when hopsAway <= threshold)
// Config value is clamped to these role-based limits
// Note: nodeinfo_direct_response must also be enabled for this to take effect
constexpr uint32_t kRouterDefaultMaxHops = 3; // Routers: max 3 hops (can set lower via config)
constexpr uint32_t kClientDefaultMaxHops = 0; // Clients: direct only (cannot increase)

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

/**
 * Return a short human-readable name for common port numbers.
 * Falls back to "port:<N>" for unknown ports.
 */
const char *portName(int portnum)
{
    switch (portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return "text";
    case meshtastic_PortNum_POSITION_APP:
        return "position";
    case meshtastic_PortNum_NODEINFO_APP:
        return "nodeinfo";
    case meshtastic_PortNum_ROUTING_APP:
        return "routing";
    case meshtastic_PortNum_ADMIN_APP:
        return "admin";
    case meshtastic_PortNum_TELEMETRY_APP:
        return "telemetry";
    case meshtastic_PortNum_TRACEROUTE_APP:
        return "traceroute";
    case meshtastic_PortNum_NEIGHBORINFO_APP:
        return "neighborinfo";
    case meshtastic_PortNum_STORE_FORWARD_APP:
        return "store-forward";
    case meshtastic_PortNum_WAYPOINT_APP:
        return "waypoint";
    default:
        return nullptr;
    }
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
    stats = meshtastic_TrafficManagementStats_init_zero;

    // Initialize rolling epoch for relative timestamps
    cacheEpochMs = millis();

    // Calculate adaptive time resolutions from config (config changes require reboot)
    // Resolution = max(60, min(339, interval/2)) for ~24 hour range with good precision
    posTimeResolution = calcTimeResolution(Default::getConfiguredOrDefault(
        moduleConfig.traffic_management.position_min_interval_secs, default_traffic_mgmt_position_min_interval_secs));
    rateTimeResolution = calcTimeResolution(moduleConfig.traffic_management.rate_limit_window_secs);
    unknownTimeResolution = calcTimeResolution(kUnknownResetMs / 1000); // ~5 min default

    const auto &cfg = moduleConfig.traffic_management;
    TM_LOG_INFO("Enabled: pos_dedup=%d nodeinfo_resp=%d rate_limit=%d drop_unknown=%d exhaust_telem=%d exhaust_pos=%d "
                "preserve_hops=%d",
                cfg.position_dedup_enabled, cfg.nodeinfo_direct_response, cfg.rate_limit_enabled, cfg.drop_unknown_enabled,
                cfg.exhaust_hop_telemetry, cfg.exhaust_hop_position, cfg.router_preserve_hops);
    TM_LOG_DEBUG("Time resolutions: pos=%us, rate=%us, unknown=%us", posTimeResolution, rateTimeResolution,
                 unknownTimeResolution);

// Allocate unified cache (10 bytes/entry for all platforms)
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    const uint16_t allocSize = cacheSize();
    TM_LOG_INFO("Allocating unified cache: %u entries (%u bytes)", allocSize,
                static_cast<unsigned>(allocSize * sizeof(UnifiedCacheEntry)));

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    // ESP32 with PSRAM: prefer PSRAM for large allocations
    cache = static_cast<UnifiedCacheEntry *>(ps_calloc(allocSize, sizeof(UnifiedCacheEntry)));
    if (cache) {
        cacheFromPsram = true;
    } else {
        TM_LOG_WARN("PSRAM allocation failed, falling back to heap");
        cache = new UnifiedCacheEntry[allocSize]();
    }
#else
    // All other platforms: heap allocation
    cache = new UnifiedCacheEntry[allocSize]();
#endif

#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE > 0

    setIntervalFromNow(kMaintenanceIntervalMs);
}

// Cache may have been allocated via ps_calloc (PSRAM, C allocator) or new[] (heap).
// Must use the matching deallocator: free() for ps_calloc, delete[] for new[].
TrafficManagementModule::~TrafficManagementModule()
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (cache) {
        if (cacheFromPsram)
            free(cache);
        else
            delete[] cache;
        cache = nullptr;
    }
#endif
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

    // Cuckoo cycle detected or max kicks exceeded.
    // The displaced entry has no valid cuckoo slot — drop it to preserve cache integrity.
    // Placing it at an arbitrary slot would make it unreachable by findEntry().
    TM_LOG_DEBUG("Cuckoo cycle, evicting node 0x%08x", displaced.node);

    if (isNew)
        *isNew = true;
    return &cache[cuckooHash1(node)];
#endif
}

// =============================================================================
// Epoch Management
// =============================================================================

/**
 * Reset the timestamp epoch when relative offsets approach overflow.
 *
 * Called when epoch age exceeds ~3.5 hours (approaching 8-bit minute overflow).
 * Invalidates all cached timestamps while preserving node associations.
 */
void TrafficManagementModule::resetEpoch(uint32_t nowMs)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    TM_LOG_DEBUG("Resetting cache epoch");
    cacheEpochMs = nowMs;

    // Invalidate all relative timestamps
    for (uint16_t i = 0; i < cacheSize(); i++) {
        cache[i].pos_time = 0;
        cache[i].rate_time = 0;
        cache[i].unknown_time = 0;
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
 * Compute 8-bit position fingerprint from truncated lat/lon coordinates.
 *
 * Unlike a hash, this is deterministic: adjacent grid cells have sequential
 * fingerprints, so nearby positions never collide. The fingerprint extracts
 * the lower 4 significant bits from each truncated coordinate.
 *
 * Example with precision=16:
 *   lat_truncated = 0x12340000 (top 16 bits significant)
 *   Significant portion = 0x1234, lower 4 bits = 0x4
 *
 * fingerprint = (lat_low4 << 4) | lon_low4 = 8 bits total
 *
 * Collision: Two positions collide only if they differ by a multiple of 16
 * grid cells in BOTH lat and lon dimensions simultaneously - very unlikely
 * for typical position update patterns.
 *
 * @param lat_truncated  Precision-truncated latitude
 * @param lon_truncated  Precision-truncated longitude
 * @param precision      Number of significant bits (1-32)
 * @return               8-bit fingerprint (4 bits lat + 4 bits lon)
 */
uint8_t TrafficManagementModule::computePositionFingerprint(int32_t lat_truncated, int32_t lon_truncated, uint8_t precision)
{
    // Guard: no truncation means no fingerprint
    if (precision == 0 || precision >= 32)
        return 0;

    // Guard: if precision < 4, we have fewer bits to work with
    // Take min(precision, 4) bits from each coordinate
    uint8_t bitsToTake = (precision < 4) ? precision : 4;

    // Shift to move significant bits to bottom, then mask lower bits
    // For precision=16: shift by 16 to get the 16 significant bits at bottom
    uint8_t shift = 32 - precision;
    uint8_t latBits = (static_cast<uint32_t>(lat_truncated) >> shift) & ((1u << bitsToTake) - 1);
    uint8_t lonBits = (static_cast<uint32_t>(lon_truncated) >> shift) & ((1u << bitsToTake) - 1);

    return static_cast<uint8_t>((latBits << 4) | lonBits);
}

// =============================================================================
// Packet Handling
// =============================================================================

// Processing order matters: this module runs BEFORE RoutingModule in the callModules() loop.
// - STOP prevents RoutingModule from calling sniffReceived() → perhapsRebroadcast(),
//   so the packet is fully consumed (not forwarded).
// - ignoreRequest suppresses the default "no one responded" NAK for want_response packets.
// - exhaustRequested is set by alterReceived() and checked by perhapsRebroadcast() to
//   force hop_limit=0 on the rebroadcast copy, allowing one final relay hop.
ProcessMessage TrafficManagementModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.has_traffic_management || !moduleConfig.traffic_management.enabled)
        return ProcessMessage::CONTINUE;

    ignoreRequest = false;
    exhaustRequested = false; // Reset per-packet; may be set by alterReceived() below
    incrementStat(&stats.packets_inspected);

    const auto &cfg = moduleConfig.traffic_management;
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
                incrementStat(&stats.unknown_packet_drops);
                ignoreRequest = true;        // Suppress NAK for want_response packets
                return ProcessMessage::STOP; // Consumed — will not be rebroadcast
            }
        }
        return ProcessMessage::CONTINUE;
    }

    // -------------------------------------------------------------------------
    // NodeInfo Direct Response
    // -------------------------------------------------------------------------
    // When we see a unicast NodeInfo request for a node we know about,
    // respond directly from cache instead of forwarding the request.
    // STOP prevents the request from being rebroadcast toward the target node,
    // and our cached response is sent back to the requestor with hop_limit=0.

    if (cfg.nodeinfo_direct_response && mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP && mp.decoded.want_response &&
        !isBroadcast(mp.to) && !isToUs(&mp) && !isFromUs(&mp)) {
        if (shouldRespondToNodeInfo(&mp, true)) {
            logAction("respond", &mp, "nodeinfo-cache");
            incrementStat(&stats.nodeinfo_cache_hits);
            ignoreRequest = true;        // We responded; suppress default NAK
            return ProcessMessage::STOP; // Consumed — request will not be forwarded
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
                    incrementStat(&stats.position_dedup_drops);
                    ignoreRequest = true;        // Suppress NAK
                    return ProcessMessage::STOP; // Consumed — duplicate will not be rebroadcast
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
                    incrementStat(&stats.rate_limit_drops);
                    ignoreRequest = true;        // Suppress NAK
                    return ProcessMessage::STOP; // Consumed — throttled packet will not be rebroadcast
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

    if (isFromUs(&mp))
        return;

    // -------------------------------------------------------------------------
    // Relayed Broadcast Hop Exhaustion
    // -------------------------------------------------------------------------
    // For relayed telemetry or position broadcasts from other nodes, optionally
    // set hop_limit=0 so they don't propagate further through the mesh.

    const auto &cfg = moduleConfig.traffic_management;
    const bool isTelemetry = mp.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    const bool isPosition = mp.decoded.portnum == meshtastic_PortNum_POSITION_APP;
    const bool shouldExhaust = (isTelemetry && cfg.exhaust_hop_telemetry) || (isPosition && cfg.exhaust_hop_position);

    if (!shouldExhaust || !isBroadcast(mp.to))
        return;

    if (mp.hop_limit > 0) {
        const char *reason = isTelemetry ? "exhaust-hop-telemetry" : "exhaust-hop-position";
        logAction("exhaust", &mp, reason);
        // Adjust hop_start so downstream nodes compute correct hopsAway (hop_start - hop_limit).
        // Without this, hop_limit=0 with original hop_start would show inflated hopsAway.
        mp.hop_start = mp.hop_start - mp.hop_limit + 1;
        mp.hop_limit = 0;
        // Signal perhapsRebroadcast() to allow one final relay with hop_limit=0.
        // Without this flag, perhapsRebroadcast() would skip the packet since hop_limit==0.
        // The flag is checked in NextHopRouter::perhapsRebroadcast() which forces
        // tosend->hop_limit=0, ensuring no further propagation beyond the next node.
        exhaustRequested = true;
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

    // Check if epoch reset needed (~3.5 hours approaching 8-bit minute overflow)
    if (needsEpochReset(nowMs)) {
        concurrency::LockGuard guard(&cacheLock);
        resetEpoch(nowMs);
        return kMaintenanceIntervalMs;
    }

    // Calculate TTLs for cache expiration
    const uint32_t positionIntervalMs = secsToMs(Default::getConfiguredOrDefault(
        moduleConfig.traffic_management.position_min_interval_secs, default_traffic_mgmt_position_min_interval_secs));
    const uint32_t positionTtlMs = positionIntervalMs * 4;

    const uint32_t rateIntervalMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);
    const uint32_t rateTtlMs = (rateIntervalMs > 0) ? rateIntervalMs * 2 : (10 * 60 * 1000UL);

    const uint32_t unknownTtlMs = kUnknownResetMs * 5;

    // Sweep cache and clear expired entries
    uint16_t activeEntries = 0;
    uint16_t expiredEntries = 0;
    const uint32_t sweepStartMs = millis();

    concurrency::LockGuard guard(&cacheLock);
    for (uint16_t i = 0; i < cacheSize(); i++) {
        if (cache[i].node == 0)
            continue;

        bool anyValid = false;

        // Check and clear expired position data
        if (cache[i].pos_time != 0) {
            uint32_t posTimeMs = fromRelativePosTime(cache[i].pos_time);
            if (!isWithinWindow(nowMs, posTimeMs, positionTtlMs)) {
                cache[i].pos_fingerprint = 0;
                cache[i].pos_time = 0;
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired rate limit data
        if (cache[i].rate_time != 0) {
            uint32_t rateTimeMs = fromRelativeRateTime(cache[i].rate_time);
            if (!isWithinWindow(nowMs, rateTimeMs, rateTtlMs)) {
                cache[i].rate_count = 0;
                cache[i].rate_time = 0;
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired unknown tracking data
        if (cache[i].unknown_time != 0) {
            uint32_t unknownTimeMs = fromRelativeUnknownTime(cache[i].unknown_time);
            if (!isWithinWindow(nowMs, unknownTimeMs, unknownTtlMs)) {
                cache[i].unknown_count = 0;
                cache[i].unknown_time = 0;
            } else {
                anyValid = true;
            }
        }

        // If all data expired, free the slot entirely
        if (!anyValid) {
            memset(&cache[i], 0, sizeof(UnifiedCacheEntry));
            expiredEntries++;
        } else {
            activeEntries++;
        }
    }

    TM_LOG_DEBUG("Maintenance: %u active, %u expired, %u/%u slots, %lums elapsed", activeEntries, expiredEntries,
                 static_cast<unsigned>(activeEntries), static_cast<unsigned>(cacheSize()),
                 static_cast<unsigned long>(millis() - sweepStartMs));

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

    uint8_t precision = Default::getConfiguredOrDefault(moduleConfig.traffic_management.position_precision_bits,
                                                        default_traffic_mgmt_position_precision_bits);
    if (precision > 32)
        precision = 32;

    const int32_t lat_truncated = truncateLatLon(pos->latitude_i, precision);
    const int32_t lon_truncated = truncateLatLon(pos->longitude_i, precision);
    const uint8_t fingerprint = computePositionFingerprint(lat_truncated, lon_truncated, precision);
    const uint32_t minIntervalMs = secsToMs(Default::getConfiguredOrDefault(
        moduleConfig.traffic_management.position_min_interval_secs, default_traffic_mgmt_position_min_interval_secs));

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    // Compare fingerprint and check time window
    // When minIntervalMs == 0, deduplication is disabled (withinInterval = false means never drop)
    const bool samePosition = !isNew && entry->pos_fingerprint == fingerprint;
    const bool withinInterval =
        (minIntervalMs == 0) ? false : isWithinWindow(nowMs, fromRelativePosTime(entry->pos_time), minIntervalMs);

    TM_LOG_DEBUG("Position dedup 0x%08x: fp=0x%02x prev=0x%02x same=%d within=%d new=%d", p->from, fingerprint,
                 entry->pos_fingerprint, samePosition, withinInterval, isNew);

    // Update cache entry
    entry->pos_fingerprint = fingerprint;
    entry->pos_time = toRelativePosTime(nowMs);

    // Drop only if same position AND within the minimum interval
    return samePosition && withinInterval;
#endif
}

bool TrafficManagementModule::shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse)
{
    // Caller already verified: nodeinfo_direct_response, portnum, want_response,
    // !isBroadcast, !isToUs, !isFromUs

    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->to);
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
    // Spoof the sender as the target node so the requestor sees a valid NodeInfo response.
    // hop_limit=0 ensures this reply travels only one hop (direct to requestor).
    reply->from = p->to;
    reply->to = getFrom(p);
    reply->channel = p->channel;
    reply->decoded.request_id = p->id;
    reply->hop_limit = 0;
    // hop_start=0 is set explicitly because Router::send() only sets it for isFromUs(),
    // and our spoofed from means isFromUs() is false.
    reply->hop_start = 0;
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

    // Both routers and clients use maxHops logic (respond when hopsAway <= threshold)
    // Role determines the maximum allowed value (enforced limit, not just default)
    bool isRouter = IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_ROUTER,
                              meshtastic_Config_DeviceConfig_Role_ROUTER_LATE, meshtastic_Config_DeviceConfig_Role_CLIENT_BASE);

    uint32_t roleLimit = isRouter ? kRouterDefaultMaxHops : kClientDefaultMaxHops;
    uint32_t configValue = moduleConfig.traffic_management.nodeinfo_direct_response_max_hops;

    // Use config value if set, otherwise use role default, but always clamp to role limit
    uint32_t maxHops = (configValue > 0) ? configValue : roleLimit;
    if (maxHops > roleLimit)
        maxHops = roleLimit;

    bool result = static_cast<uint32_t>(hopsAway) <= maxHops;
    TM_LOG_DEBUG("NodeInfo hops check: hopsAway=%d maxHops=%u roleLimit=%u isRouter=%d -> %s", hopsAway, maxHops, roleLimit,
                 isRouter, result ? "respond" : "skip");
    return result;
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
    if (isNew || !isWithinWindow(nowMs, fromRelativeRateTime(entry->rate_time), windowMs)) {
        entry->rate_time = toRelativeRateTime(nowMs);
        entry->rate_count = 1;
        return false;
    }

    // Increment counter (saturates at 255)
    saturatingIncrement(entry->rate_count);

    // Check against threshold (uint8_t max is 255, but config is uint32_t)
    uint32_t threshold = moduleConfig.traffic_management.rate_limit_max_packets;
    if (threshold > 255)
        threshold = 255;

    bool limited = entry->rate_count > threshold;
    if (limited || entry->rate_count == threshold) {
        TM_LOG_DEBUG("Rate limit 0x%08x: count=%u threshold=%u -> %s", from, entry->rate_count, threshold,
                     limited ? "DROP" : "at-limit");
    }
    return limited;
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
    if (isNew || !isWithinWindow(nowMs, fromRelativeUnknownTime(entry->unknown_time), windowMs)) {
        entry->unknown_time = toRelativeUnknownTime(nowMs);
        entry->unknown_count = 0;
    }

    // Increment counter (saturates at 255)
    saturatingIncrement(entry->unknown_count);

    // Check against threshold
    uint32_t threshold = moduleConfig.traffic_management.unknown_packet_threshold;
    if (threshold > 255)
        threshold = 255;

    bool drop = entry->unknown_count > threshold;
    if (drop || entry->unknown_count == threshold) {
        TM_LOG_DEBUG("Unknown packets 0x%08x: count=%u threshold=%u -> %s", p->from, entry->unknown_count, threshold,
                     drop ? "DROP" : "at-limit");
    }
    return drop;
#endif
}

void TrafficManagementModule::logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const
{
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        const char *name = portName(p->decoded.portnum);
        if (name) {
            TM_LOG_INFO("%s %s from=0x%08x to=0x%08x hop=%d/%d reason=%s", action, name, getFrom(p), p->to, p->hop_limit,
                        p->hop_start, reason);
        } else {
            TM_LOG_INFO("%s port=%d from=0x%08x to=0x%08x hop=%d/%d reason=%s", action, p->decoded.portnum, getFrom(p), p->to,
                        p->hop_limit, p->hop_start, reason);
        }
    } else {
        TM_LOG_INFO("%s encrypted from=0x%08x to=0x%08x hop=%d/%d reason=%s", action, getFrom(p), p->to, p->hop_limit,
                    p->hop_start, reason);
    }
}

#endif
