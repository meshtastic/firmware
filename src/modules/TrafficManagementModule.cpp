#include "TrafficManagementModule.h"

#if HAS_TRAFFIC_MANAGEMENT

#include "Channels.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PositionPrecision.h"
#include "Router.h"
#include "TypeConversions.h"
#include "airtime.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "memory/MemAudit.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include <Arduino.h>
#include <algorithm>
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

// Advertised role of the originating node (from NodeDB), or CLIENT (no exception) if unknown.
// Position filtering grants two role exceptions: trackers may refresh duplicates hourly, and
// lost-and-found is throttled only to the shortest dedup window. Both are still subject to
// the channel-precision ceiling in alterReceived().
meshtastic_Config_DeviceConfig_Role originRole(NodeNum from)
{
    // Resolve via NodeDB: hot store (with user) → warm-tier cached role → CLIENT. The
    // warm fallback keeps role exceptions firing for trackers/etc. aged out of the hot store.
    return nodeDB ? nodeDB->getNodeRole(from) : meshtastic_Config_DeviceConfig_Role_CLIENT;
}

/**
 * Clamp precision to a valid dedup range.
 * Invalid values use the module default precision.
 */
uint8_t sanitizePositionPrecision(uint8_t precision)
{
    if (precision > 0 && precision <= 32)
        return precision;

    const uint8_t defaultPrecision = static_cast<uint8_t>(default_traffic_mgmt_position_precision_bits);
    if (defaultPrecision > 0 && defaultPrecision <= 32)
        return defaultPrecision;

    // Someone done messed up if we reach here
    return 32;
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

    const auto &cfg = moduleConfig.traffic_management;
    TM_LOG_INFO("Config: nodeinfo_max_hops=%u rate_window=%us rate_max=%u unknown_thresh=%u pos_interval=%us",
                cfg.nodeinfo_direct_response_max_hops, cfg.rate_limit_window_secs, cfg.rate_limit_max_packets,
                cfg.unknown_packet_threshold, cfg.position_min_interval_secs);

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

    memaudit::set("tmm", cache ? allocSize * sizeof(UnifiedCacheEntry) : 0);
#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE > 0

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    TM_LOG_INFO("Allocating NodeInfo cache: %u entries, %u bytes (PSRAM flat array)",
                static_cast<unsigned>(nodeInfoTargetEntries()),
                static_cast<unsigned>(nodeInfoTargetEntries() * sizeof(NodeInfoPayloadEntry)));

    nodeInfoPayload = static_cast<NodeInfoPayloadEntry *>(ps_calloc(nodeInfoTargetEntries(), sizeof(NodeInfoPayloadEntry)));
    if (nodeInfoPayload) {
        nodeInfoPayloadFromPsram = true;
        TM_LOG_INFO("NodeInfo PSRAM cache ready");
    } else {
        TM_LOG_WARN("NodeInfo PSRAM payload allocation failed; direct responses will fall back to NodeDB");
    }
    memaudit::set("tmm_ni", nodeInfoPayload ? nodeInfoTargetEntries() * sizeof(NodeInfoPayloadEntry) : 0);
#else
    TM_LOG_DEBUG("NodeInfo PSRAM cache not available on this target");
#endif

    setIntervalFromNow(kMaintenanceIntervalMs);
}

// Cache may have been allocated via ps_calloc (PSRAM, C allocator) or new[] (heap).
// Must use the matching deallocator: free() for ps_calloc, delete[] for new[].
TrafficManagementModule::~TrafficManagementModule()
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (cache) {
        // Cache may be from ps_calloc (PSRAM, C allocator) or new[] (heap).
        // Use the matching deallocator for the allocation source.
        if (cacheFromPsram)
            free(cache);
        else
            delete[] cache;
        cache = nullptr;
    }
    memaudit::set("tmm", 0);
#endif

    if (nodeInfoPayload) {
        if (nodeInfoPayloadFromPsram)
            free(nodeInfoPayload);
        else
            delete[] nodeInfoPayload;
        nodeInfoPayload = nullptr;
    }
    memaudit::set("tmm_ni", 0);
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
    // router_preserve_hops: not suitable right now - removed from config until
    // the right heuristic for when to preserve vs. exhaust is clearer.
    (void)stats.router_hops_preserved;
}

void TrafficManagementModule::incrementStat(uint32_t *field)
{
    concurrency::LockGuard guard(&cacheLock);
    (*field)++;
}

// =============================================================================
// Flat Unified Cache Operations
// =============================================================================

/**
 * Find an existing entry for the given node (linear scan).
 */
TrafficManagementModule::UnifiedCacheEntry *TrafficManagementModule::findEntry(NodeNum node)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)node;
    return nullptr;
#else
    if (!cache || node == 0)
        return nullptr;

    for (uint16_t i = 0; i < cacheSize(); i++) {
        if (cache[i].node == node)
            return &cache[i];
    }
    return nullptr;
#endif
}

int TrafficManagementModule::peekCachedRole(NodeNum node)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)node;
    return -1;
#else
    concurrency::LockGuard guard(&cacheLock);
    const UnifiedCacheEntry *entry = findEntry(node);
    return entry ? static_cast<int>(entry->getCachedRole()) : -1;
#endif
}

/**
 * Find or create an entry for the given node.
 *
 * One linear pass tracks the match, the first empty slot, and the eviction
 * victim. When the cache is full, the victim is the stalest entry (largest
 * of its three relative timestamps is smallest), preferring entries without
 * a next_hop hint - those hints are the long-tail routing state the cache
 * exists to keep, and the maintenance sweep never ages them out.
 *
 * @param node  NodeNum to find or create
 * @param isNew Set to true if a new entry was created
 * @return      Pointer to entry, or nullptr if the cache is unavailable
 */
// Sender-role resolution for the position hot path. The tier-3 cache is authoritative
// here and is kept fresh by updateCachedRoleFromNodeInfo() - i.e. updated at the same
// time NodeDB learns a role, not re-derived on every packet. We only fall back to a
// NodeDB scan (tiers 1+2) the first time we start tracking a node, to seed the cache so
// a resident special-role node is correct from its very first position. Thereafter the
// read is O(1) and survives the node aging out of both NodeDB stores.
meshtastic_Config_DeviceConfig_Role TrafficManagementModule::resolveSenderRole(NodeNum from, UnifiedCacheEntry *entry, bool isNew)
{
    if (!entry)
        return originRole(from);
    if (isNew) {
        // First time tracking this node: seed tier 3 from NodeDB (hot → warm). Stores
        // CLIENT (0) too, which simply reads back as "no exception".
        const meshtastic_Config_DeviceConfig_Role role = originRole(from);
        entry->setCachedRole(static_cast<uint8_t>(std::min(15, static_cast<int>(role))));
        return role;
    }
    // Established entry: trust the cached role (refreshed on NodeInfo). No NodeDB scan.
    return static_cast<meshtastic_Config_DeviceConfig_Role>(entry->getCachedRole());
}

// Refresh the tier-3 role cache from an observed NodeInfo - the same event that updates
// NodeDB's role - so role changes (including demotion back to CLIENT) are picked up
// without scanning NodeDB on the position hot path. Role is read straight from the
// packet's User payload (authoritative regardless of module ordering). Only updates nodes
// we already track (findEntry, no create) so NodeInfo from non-position nodes can't pollute
// the cache; the role rides along with the node's existing position/rate/unknown state.
void TrafficManagementModule::updateCachedRoleFromNodeInfo(const meshtastic_MeshPacket &mp)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (mp.decoded.payload.size == 0)
        return;
    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user))
        return;

    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findEntry(getFrom(&mp));
    if (entry)
        entry->setCachedRole(static_cast<uint8_t>(std::min(15, static_cast<int>(user.role))));
#else
    (void)mp;
#endif
}

TrafficManagementModule::UnifiedCacheEntry *TrafficManagementModule::findOrCreateEntry(NodeNum node, bool *isNew)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE == 0
    (void)node;
    if (isNew)
        *isNew = false;
    return nullptr;
#else
    if (isNew)
        *isNew = false;
    if (!cache || node == 0)
        return nullptr;

    UnifiedCacheEntry *empty = nullptr;
    UnifiedCacheEntry *victim = nullptr;
    bool leastPreferredVictim = true;
    uint8_t victimRecency = UINT8_MAX;

    for (uint16_t i = 0; i < cacheSize(); i++) {
        UnifiedCacheEntry &e = cache[i];
        if (e.node == node)
            return &e;
        if (e.node == 0) {
            if (!empty)
                empty = &e;
            continue;
        }
        if (empty)
            continue; // an empty slot beats any victim; stop scoring
        // "Preferred" entries are evicted last: a confirmed next-hop hint (routing overflow
        // store) or a cached special (non-CLIENT) role (tracker / lost-and-found / router).
        // Both are the long-tail state this cache exists to retain.
        const bool preferred = e.next_hop != 0 || e.getCachedRole() != meshtastic_Config_DeviceConfig_Role_CLIENT;
        // Age in pos-ticks (8-bit modular, wraps correctly). Entries with no
        // pos state (pos_time==0) score as maximally old (age=currentPosTick()).
        const uint8_t nowPosTick = currentPosTick();
        const uint8_t posAge = static_cast<uint8_t>(nowPosTick - e.pos_time);
        // Blend in rate/unknown ages scaled to pos-tick units (coarser = conservative).
        const uint8_t rateAgePosScale =
            static_cast<uint8_t>(static_cast<uint8_t>((currentRateTick() - e.getRateTime()) & 0x0F) * 5 / 3);
        const uint8_t unknownAgePosScale =
            static_cast<uint8_t>(static_cast<uint8_t>((currentUnknownTick() - e.getUnknownTime()) & 0x0F) / 6);
        uint8_t recencyAge = posAge;
        if (e.getRateCount() != 0 && rateAgePosScale > recencyAge)
            recencyAge = rateAgePosScale;
        if (e.getUnknownCount() != 0 && unknownAgePosScale > recencyAge)
            recencyAge = unknownAgePosScale;
        const uint8_t recency = static_cast<uint8_t>(UINT8_MAX - recencyAge);
        if (!victim || (preferred == leastPreferredVictim ? recency < victimRecency : !preferred)) {
            victim = &e;
            leastPreferredVictim = preferred;
            victimRecency = recency;
        }
    }

    UnifiedCacheEntry *slot = empty ? empty : victim;
    if (!slot)
        return nullptr;
    if (!empty)
        TM_LOG_DEBUG("Unified cache full, evicting node 0x%08x", slot->node);
    memset(slot, 0, sizeof(UnifiedCacheEntry));
    slot->node = node;
    if (isNew)
        *isNew = true;
    return slot;
#endif
}

const TrafficManagementModule::NodeInfoPayloadEntry *TrafficManagementModule::findNodeInfoEntry(NodeNum node) const
{
#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    if (!nodeInfoPayload || node == 0)
        return nullptr;

    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        if (nodeInfoPayload[i].node == node)
            return &nodeInfoPayload[i];
    }
    return nullptr;
#else
    (void)node;
    return nullptr;
#endif
}

/**
 * Find or create a NodeInfo payload entry (linear scan of the flat PSRAM
 * array). One pass tracks the match, the first empty slot, and the LRU
 * victim by lastObservedMs (wrap-safe age). NodeInfo traffic is low-rate,
 * so the O(n) scan is negligible.
 */
TrafficManagementModule::NodeInfoPayloadEntry *TrafficManagementModule::findOrCreateNodeInfoEntry(NodeNum node,
                                                                                                  bool *usedEmptySlot)
{
    if (usedEmptySlot)
        *usedEmptySlot = false;

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    if (!nodeInfoPayload || node == 0)
        return nullptr;

    NodeInfoPayloadEntry *empty = nullptr;
    NodeInfoPayloadEntry *lru = nullptr;
    uint32_t lruAge = 0;
    const uint32_t now = clockMs();

    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        NodeInfoPayloadEntry &e = nodeInfoPayload[i];
        if (e.node == node)
            return &e;
        if (e.node == 0) {
            if (!empty)
                empty = &e;
            continue;
        }
        if (empty)
            continue;                                // an empty slot beats any victim; stop scoring
        const uint32_t age = now - e.lastObservedMs; // unsigned subtraction is wrap-safe
        if (!lru || age > lruAge) {
            lru = &e;
            lruAge = age;
        }
    }

    NodeInfoPayloadEntry *slot = empty ? empty : lru;
    if (!slot)
        return nullptr;
    memset(slot, 0, sizeof(NodeInfoPayloadEntry));
    slot->node = node;
    if (usedEmptySlot)
        *usedEmptySlot = (slot == empty);
    return slot;
#else
    (void)node;
    return nullptr;
#endif
}

uint16_t TrafficManagementModule::countNodeInfoEntriesLocked() const
{
#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    if (!nodeInfoPayload)
        return 0;

    uint16_t count = 0;
    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        if (nodeInfoPayload[i].node != 0)
            count++;
    }
    return count;
#else
    return 0;
#endif
}

void TrafficManagementModule::cacheNodeInfoPacket(const meshtastic_MeshPacket &mp)
{
#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    if (!nodeInfoPayload || mp.decoded.payload.size == 0)
        return;

    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user))
        return;

    // Normalize user.id to the packet sender's node number.
    snprintf(user.id, sizeof(user.id), "!%08x", getFrom(&mp));

    bool usedEmptySlot = false;
    uint16_t cachedCount = 0;
    {
        concurrency::LockGuard guard(&cacheLock);
        NodeInfoPayloadEntry *entry = findOrCreateNodeInfoEntry(getFrom(&mp), &usedEmptySlot);
        if (!entry)
            return;

        // Cache both payload and response metadata so direct replies can use
        // richer context than "just the user protobuf" when PSRAM is present.
        // This path is intentionally independent from NodeInfoModule/NodeDB.
        entry->user = user;
        entry->lastObservedMs = clockMs();
        entry->lastObservedRxTime = mp.rx_time;
        entry->sourceChannel = mp.channel;
        entry->hasDecodedBitfield = mp.decoded.has_bitfield;
        entry->decodedBitfield = mp.decoded.bitfield;

        if (usedEmptySlot)
            cachedCount = countNodeInfoEntriesLocked();
    }

    if (usedEmptySlot) {
        TM_LOG_INFO("NodeInfo PSRAM cache entries: %u/%u", static_cast<unsigned>(cachedCount),
                    static_cast<unsigned>(nodeInfoTargetEntries()));
    }
#else
    (void)mp;
#endif
}

// =============================================================================
// Next-Hop Overflow Cache
// =============================================================================
//
// A routing hint store. The byte is the last byte of the NodeNum to use as next
// hop to reach `dest`. It is written ONLY from NextHopRouter's ACK-confirmed
// decision (a bidirectionally-verified relay) - never inferred one-way from
// relayed traffic. The TMM cache holds confirmed next-hops that have aged out of
// the hot NodeDB (NodeInfoLite), and NextHopRouter::getNextHop() consults it as a
// fallback after the hot store.

void TrafficManagementModule::setNextHop(NodeNum dest, uint8_t nextHopByte)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (!cache || dest == 0 || nextHopByte == 0)
        return;

    concurrency::LockGuard guard(&cacheLock);
    bool isNew = false;
    UnifiedCacheEntry *entry = findOrCreateEntry(dest, &isNew);
    if (entry)
        entry->next_hop = nextHopByte; // last-write-wins; only confirmed bytes reach here
#else
    (void)dest;
    (void)nextHopByte;
#endif
}

uint8_t TrafficManagementModule::getNextHopHint(NodeNum dest)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (!cache || dest == 0)
        return 0;

    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findEntry(dest);
    return entry ? entry->next_hop : 0;
#else
    (void)dest;
    return 0;
#endif
}

void TrafficManagementModule::clearNextHop(NodeNum dest)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (!cache || dest == 0)
        return;

    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findEntry(dest);
    if (entry)
        entry->next_hop = 0; // keep the entry (other stats), just drop the routing hint
#else
    (void)dest;
#endif
}

bool TrafficManagementModule::preloadNextHopsFromNodeDB()
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (!cache || !nodeDB)
        return false; // prerequisites not ready yet - caller should retry on a later pass

    uint16_t seeded = 0;
    concurrency::LockGuard guard(&cacheLock);
    const size_t count = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < count; i++) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == 0 || node->next_hop == 0)
            continue;

        bool isNew = false;
        UnifiedCacheEntry *entry = findOrCreateEntry(node->num, &isNew);
        // Don't clobber a freshly-learned confirmed hop with a (possibly stale) persisted one.
        if (entry && entry->next_hop == 0) {
            entry->next_hop = node->next_hop;
            seeded++;
        }
    }

    TM_LOG_INFO("Preloaded %u next-hop hints from NodeDB", static_cast<unsigned>(seeded));
    return true;
#else
    return true; // nothing to preload on a cache-less build; don't keep retrying
#endif
}

// =============================================================================
// Epoch Management
// =============================================================================

void TrafficManagementModule::flushCache()
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    TM_LOG_DEBUG("Flushing cache");
    memset(cache, 0, static_cast<size_t>(cacheSize()) * sizeof(UnifiedCacheEntry));
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
    precision = sanitizePositionPrecision(precision);

    // Guard: if precision < 4, we have fewer bits to work with
    // Take min(precision, 4) bits from each coordinate
    uint8_t bitsToTake = (precision < 4) ? precision : 4;

    // Shift to move significant bits to bottom, then mask lower bits
    // For precision=16: shift by 16 to get the 16 significant bits at bottom
    uint8_t shift = 32 - precision;
    uint8_t latBits = (static_cast<uint32_t>(lat_truncated) >> shift) & ((1u << bitsToTake) - 1);
    uint8_t lonBits = (static_cast<uint32_t>(lon_truncated) >> shift) & ((1u << bitsToTake) - 1);

    const uint8_t fp = static_cast<uint8_t>((latBits << 4) | lonBits);
    // 0 is the "no position seen" sentinel for pos_fingerprint, so a real position that happens to
    // hash to 0 must not collide with it (otherwise its duplicates would never dedup). Remap 0 -> 0xFF,
    // mirroring NodeDB::getLastByteOfNodeNum()'s 0 -> 0xFF idiom. Cost: the 0x00 bucket merges into
    // 0xFF (one extra collision in 256 - negligible; the fingerprint already collides every 16 cells).
    return fp ? fp : 0xFF;
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
    if (!moduleConfig.has_traffic_management)
        return ProcessMessage::CONTINUE;

    ignoreRequest = false;
    exhaustRequested = false; // Reset per-packet; may be set by alterReceived() below
    exhaustRequestedFrom = 0;
    exhaustRequestedId = 0;
    incrementStat(&stats.packets_inspected);

    const auto &cfg = moduleConfig.traffic_management;
    const uint32_t nowMs = TrafficManagementModule::clockMs();

    // -------------------------------------------------------------------------
    // Undecoded Packet Handling
    // -------------------------------------------------------------------------
    // Packets we can't decode (wrong key, corruption, etc.) may indicate
    // a misbehaving node. Track and optionally drop repeat offenders.

    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        if (cfg.unknown_packet_threshold > 0) {
            if (shouldDropUnknown(&mp, nowMs)) {
                logAction("drop", &mp, "unknown");
                incrementStat(&stats.unknown_packet_drops);
                ignoreRequest = true;        // Suppress NAK for want_response packets
                return ProcessMessage::STOP; // Consumed - will not be rebroadcast
            }
        }
        return ProcessMessage::CONTINUE;
    }

    // Learn NodeInfo payloads into the dedicated PSRAM cache, and refresh the tier-3
    // role cache for any node we already track (keeps the dedup role exception current).
    if (mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP) {
        cacheNodeInfoPacket(mp);
        updateCachedRoleFromNodeInfo(mp);
    }

    // -------------------------------------------------------------------------
    // NodeInfo Direct Response
    // -------------------------------------------------------------------------
    // When we see a unicast NodeInfo request for a node we know about,
    // respond directly from cache instead of forwarding the request.
    // STOP prevents the request from being rebroadcast toward the target node,
    // and our cached response is sent back to the requestor with hop_limit=0.

    if (cfg.nodeinfo_direct_response_max_hops > 0 && mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
        mp.decoded.want_response && !isBroadcast(mp.to) && !isToUs(&mp) && !isFromUs(&mp)) {
        if (shouldRespondToNodeInfo(&mp, true)) {
            meshtastic_User requester = meshtastic_User_init_zero;
            if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &requester)) {
                nodeDB->updateUser(getFrom(&mp), requester, mp.channel);
            }
            logAction("respond", &mp, "nodeinfo-cache");
            incrementStat(&stats.nodeinfo_cache_hits);
            ignoreRequest = true;        // We responded; suppress default NAK
            return ProcessMessage::STOP; // Consumed - request will not be forwarded
        }
    }

    // -------------------------------------------------------------------------
    // Position Deduplication
    // -------------------------------------------------------------------------
    // Drop position broadcasts that haven't moved significantly since the
    // last broadcast from this node. Uses truncated coordinates to ignore
    // GPS jitter within the configured precision.

    if (!isFromUs(&mp) && !isToUs(&mp)) {
        if (channels.isWellKnownChannel(mp.channel) && mp.decoded.portnum == meshtastic_PortNum_POSITION_APP) {
            meshtastic_Position pos = meshtastic_Position_init_zero;
            if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &pos)) {
                if (shouldDropPosition(&mp, &pos, nowMs)) {
                    logAction("drop", &mp, "position-dedup");
                    incrementStat(&stats.position_dedup_drops);
                    ignoreRequest = true;        // Suppress NAK
                    return ProcessMessage::STOP; // Consumed - duplicate will not be rebroadcast
                }
            }
        }

        // ---------------------------------------------------------------------
        // Rate Limiting
        // ---------------------------------------------------------------------
        // Throttle nodes sending too many packets within a time window.
        // Excludes routing and admin packets which are essential for mesh operation.

        if (cfg.rate_limit_window_secs > 0 && cfg.rate_limit_max_packets > 0) {
            if (mp.decoded.portnum != meshtastic_PortNum_ROUTING_APP && mp.decoded.portnum != meshtastic_PortNum_ADMIN_APP) {
                if (isRateLimited(mp.from, nowMs)) {
                    logAction("drop", &mp, "rate-limit");
                    incrementStat(&stats.rate_limit_drops);
                    ignoreRequest = true;        // Suppress NAK
                    return ProcessMessage::STOP; // Consumed - throttled packet will not be rebroadcast
                }
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

void TrafficManagementModule::alterReceived(meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.has_traffic_management)
        return;

    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return;

    if (isFromUs(&mp))
        return;

    // exhaust_hop_telemetry / exhaust_hop_position / router_preserve_hops:
    // not suitable right now - the right heuristics for when to exhaust or
    // preserve hops need more field data before we expose them as config knobs.
    // exhaustRequested stays false; perhapsRebroadcast() behaves normally.

    const bool isPosition = mp.decoded.portnum == meshtastic_PortNum_POSITION_APP;

    // -------------------------------------------------------------------------
    // Relayed Position Precision Clamp
    // -------------------------------------------------------------------------
    // Clamp relayed position broadcasts to the channel's configured precision
    // ceiling. Guards against forwarding more-precise coordinates than the
    // channel is intended to carry (e.g. a LongFast channel set to 13-bit /
    // ~1.5 km). chanPrec==0 means position sharing is disabled on the channel;
    // skip - not our job to zero positions on relay.
    // Ham mode (owner.is_licensed) is exempt. Lost-and-found is NOT exempt - its relayed
    // positions get the same precision clamp as any node.
    // Compile USERPREFS_TMM_APPLY_TO_PRIVATE_CHANNELS to extend to private channels.
    if (!owner.is_licensed && isPosition && isBroadcast(mp.to)) {
#ifdef USERPREFS_TMM_APPLY_TO_PRIVATE_CHANNELS
        const bool shouldClamp = true;
#else
        const bool shouldClamp = channels.isWellKnownChannel(mp.channel);
#endif
        if (shouldClamp) {
            const uint32_t chanPrec = getPositionPrecisionForChannel(mp.channel);
            if (chanPrec > 0) {
                meshtastic_Position pos = meshtastic_Position_init_default;
                if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &pos)) {
                    const uint32_t packetPrec = pos.precision_bits > 0 ? pos.precision_bits : 32u;
                    if (packetPrec > chanPrec) {
                        applyPositionPrecision(pos, chanPrec);
                        mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                                     &meshtastic_Position_msg, &pos);
                        logAction("clamp", &mp, "precision");
                    }
                }
            }
        }
    }
}

// =============================================================================
// Periodic Maintenance
// =============================================================================

int32_t TrafficManagementModule::runOnce()
{
    if (!moduleConfig.has_traffic_management)
        return INT32_MAX;

#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    const uint32_t nowMs = TrafficManagementModule::clockMs();

    // Warm-start the next-hop cache from persisted NodeInfoLite hints once nodeDB
    // is populated. Done here (not in the constructor) so nodeDB has finished
    // loading. Takes its own lock, so call before acquiring the sweep guard below.
    // Only latch the one-shot guard once the preload actually ran; if nodeDB wasn't
    // ready yet, retry on the next maintenance pass instead of skipping it forever.
    if (!nextHopPreloaded && preloadNextHopsFromNodeDB())
        nextHopPreloaded = true;

    // Free-running tick counters (no epoch needed).
    // TTL expressed in ticks:
    // pos: 4× position_min_interval_secs (clamped to 255 ticks @ 6 min/tick)
    // rate: 2× rate_limit_window_secs (clamped to 15 ticks @ 5 min/tick; only relevant when rate limits are configured)
    // unknown: fixed 12 ticks @ 1 min/tick (only relevant when unknown_packet_threshold > 0)
    const uint32_t positionIntervalMs = secsToMs(Default::getConfiguredOrDefault(
        moduleConfig.traffic_management.position_min_interval_secs, default_traffic_mgmt_position_min_interval_secs));
    const uint8_t posTtlTicks =
        static_cast<uint8_t>(std::min(static_cast<uint32_t>(255), (positionIntervalMs * 4) / kPosTimeTickMs));

    const uint32_t rateWindowMs = secsToMs(moduleConfig.traffic_management.rate_limit_window_secs);
    const uint8_t rateTtlTicks = static_cast<uint8_t>(
        std::min(static_cast<uint32_t>(15), (rateWindowMs > 0 ? rateWindowMs * 2 : 24 * kRateTimeTickMs) / kRateTimeTickMs));

    // unknown: fixed 12-tick TTL (12 min - 4 ticks past the 5-min default window)
    const uint8_t unknownTtlTicks = 12;

    const uint8_t nowPosTick = currentPosTick();
    const uint8_t nowRateTick = currentRateTick();
    const uint8_t nowUnknownTick = currentUnknownTick();

    // Sweep cache and clear expired entries
    uint16_t activeEntries = 0;
    uint16_t expiredEntries = 0;
    const uint32_t sweepStartMs = TrafficManagementModule::clockMs();

    const auto &cfg = moduleConfig.traffic_management;
    concurrency::LockGuard guard(&cacheLock);

    for (uint16_t i = 0; i < cacheSize(); i++) {
        if (cache[i].node == 0)
            continue;

        bool anyValid = false;

        // Check and clear expired position data (presence: pos_fingerprint != 0)
        if (cache[i].pos_fingerprint != 0) {
            if (static_cast<uint8_t>(nowPosTick - cache[i].pos_time) >= posTtlTicks) {
                cache[i].pos_fingerprint = 0;
                cache[i].pos_time = 0;
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired rate limit data (presence: getRateCount() != 0)
        if (cache[i].getRateCount() != 0) {
            if ((static_cast<uint8_t>(nowRateTick - cache[i].getRateTime()) & 0x0F) >= rateTtlTicks) {
                cache[i].setRateCount(0);
                cache[i].setRateTime(0);
            } else {
                anyValid = true;
            }
        }

        // Check and clear expired unknown tracking data (presence: getUnknownCount() != 0)
        if (cache[i].getUnknownCount() != 0) {
            if ((static_cast<uint8_t>(nowUnknownTick - cache[i].getUnknownTime()) & 0x0F) >= unknownTtlTicks) {
                cache[i].setUnknownCount(0);
                cache[i].setUnknownTime(0);
            } else {
                anyValid = true;
            }
        }

        // Two fields have no TTL of their own and pin the slot, so they outlive the
        // dedup/rate/unknown state:
        //   - a confirmed next-hop hint (the routing overflow store), and
        //   - a cached special (non-CLIENT) role, so a tracker / lost-and-found / router
        //     keeps its dedup-window exception across quiet periods rather than reverting
        //     to CLIENT the moment its timed state expires.
        if (cache[i].next_hop != 0 || cache[i].getCachedRole() != meshtastic_Config_DeviceConfig_Role_CLIENT)
            anyValid = true;

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
                 static_cast<unsigned long>(TrafficManagementModule::clockMs() - sweepStartMs));

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    if (nodeInfoPayload) {
        TM_LOG_DEBUG("NodeInfo PSRAM cache: %u/%u", static_cast<unsigned>(countNodeInfoEntriesLocked()),
                     static_cast<unsigned>(nodeInfoTargetEntries()));
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

    // Precision is driven by the channel's own position_precision ceiling - the same
    // grid the channel uses for broadcast. Falls back to the firmware default (19-bit,
    // ~90m cells) when the channel has no precision configured (chanPrec == 0).
    const uint32_t chanPrec = getPositionPrecisionForChannel(p->channel);
    uint8_t precision = sanitizePositionPrecision(
        chanPrec > 0 ? static_cast<uint8_t>(chanPrec) : static_cast<uint8_t>(default_traffic_mgmt_position_precision_bits));

    const int32_t lat_truncated = truncateCoordinate(pos->latitude_i, precision);
    const int32_t lon_truncated = truncateCoordinate(pos->longitude_i, precision);
    const uint8_t fingerprint = computePositionFingerprint(lat_truncated, lon_truncated, precision);
    // Drop gate uses the RAW configured interval: 0 means "dedup disabled" (the
    // contract documented below). The 12h default is only for resolution/TTL
    // sizing (constructor / runOnce), not for deciding whether to drop - feeding
    // the default here would silently turn the 0-disables-dedup contract off.
    uint32_t minIntervalMs = secsToMs(moduleConfig.traffic_management.position_min_interval_secs);

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    // Role exceptions keyed on the originating node's advertised role, resolved across
    // all three tiers (hot store → warm store → TMM live cache). The position path is
    // the one place that needs sender-role, and it also keeps tier 3 warm so the
    // exception survives the node aging out of both NodeDB stores - important in the
    // common dedup-only config, where isRateLimited()'s role write never runs.
    const meshtastic_Config_DeviceConfig_Role role = resolveSenderRole(p->from, entry, isNew);
    if (role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND) {
        // Lost-and-found may refresh a duplicate position at most every ~15 min (cap, never
        // lengthens; quantised to ~2 dedup ticks). Only when dedup is active - never tighten
        // past an operator who disabled it (0).
        const uint32_t lostFoundCapMs = secsToMs(default_traffic_mgmt_lost_and_found_position_min_interval_secs);
        if (minIntervalMs != 0 && minIntervalMs > lostFoundCapMs)
            minIntervalMs = lostFoundCapMs;
    } else if (role == meshtastic_Config_DeviceConfig_Role_TRACKER || role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) {
        // Trackers may refresh a duplicate position as often as hourly (cap, never lengthens).
        const uint32_t trackerCapMs = secsToMs(default_traffic_mgmt_tracker_position_min_interval_secs);
        if (minIntervalMs > trackerCapMs)
            minIntervalMs = trackerCapMs;
    }

    // Compare fingerprint and check time window.
    // When minIntervalMs == 0, deduplication is disabled (withinInterval = false means never drop).
    // Presence: pos_fingerprint != 0; computePositionFingerprint() remaps 0 -> 0xFF so zero means unseen.
    const bool hasPositionState = !isNew && entry->pos_fingerprint != 0;
    const bool samePosition = hasPositionState && entry->pos_fingerprint == fingerprint;
    const uint8_t nowPosTick = currentPosTick();
    // Clamp to [1, 255]: intervals shorter than one tick still dedup within the same tick.
    const uint8_t windowTicks =
        (minIntervalMs == 0) ? 0
                             : static_cast<uint8_t>(std::min(static_cast<uint32_t>(UINT8_MAX),
                                                             std::max(static_cast<uint32_t>(1), minIntervalMs / kPosTimeTickMs)));
    const bool withinInterval =
        hasPositionState && (windowTicks != 0) && (static_cast<uint8_t>(nowPosTick - entry->pos_time) < windowTicks);

    TM_LOG_DEBUG("Position dedup 0x%08x: fp=0x%02x prev=0x%02x same=%d within=%d new=%d", p->from, fingerprint,
                 entry->pos_fingerprint, samePosition, withinInterval, isNew);

    // Update cache entry (raw tick; 0 is a valid tick value)
    entry->pos_fingerprint = fingerprint;
    entry->pos_time = nowPosTick;

    // Drop only if same position AND within the minimum interval
    return samePosition && withinInterval;
#endif
}

bool TrafficManagementModule::shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse)
{
    // Caller already verified: nodeinfo_direct_response, portnum, want_response,
    // !isBroadcast, !isToUs, !isFromUs

    if (!isMinHopsFromRequestor(p))
        return false;

    meshtastic_User cachedUser = meshtastic_User_init_zero;
    bool hasCachedUser = false;

    // Extra metadata consumed only by the PSRAM-backed cache path.
    // Defaults preserve previous behavior when cache metadata is unavailable.
    bool cachedHasDecodedBitfield = false;
    uint8_t cachedDecodedBitfield = 0;
    uint8_t cachedSourceChannel = 0;
    uint32_t cachedLastObservedMs = 0;
    uint32_t cachedLastObservedRxTime = 0;

    {
        concurrency::LockGuard guard(&cacheLock);
        const NodeInfoPayloadEntry *entry = findNodeInfoEntry(p->to);
        if (entry) {
            cachedUser = entry->user;
            hasCachedUser = true;
            cachedHasDecodedBitfield = entry->hasDecodedBitfield;
            cachedDecodedBitfield = entry->decodedBitfield;
            cachedSourceChannel = entry->sourceChannel;
            cachedLastObservedMs = entry->lastObservedMs;
            cachedLastObservedRxTime = entry->lastObservedRxTime;
        }
    }

    if (!hasCachedUser) {
        // If the PSRAM cache exists but misses, we intentionally do not fall back
        // to the node-wide table. This keeps the PSRAM direct-reply path separate
        // from NodeInfoModule/NodeDB behavior when PSRAM is available.
        if (nodeInfoPayload) {
            TM_LOG_DEBUG("NodeInfo PSRAM cache miss for node=0x%08x", p->to);
            return false;
        }

        // Fallback only when PSRAM cache is unavailable on this target.
        // In this mode we use the node-wide table maintained by NodeInfoModule.
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->to);
        if (!nodeInfoLiteHasUser(node))
            return false;
        cachedUser = TypeConversions::ConvertToUser(node);
    }

    if (!sendResponse)
        return true;

    meshtastic_MeshPacket *reply = router->allocForSending();
    if (!reply) {
        TM_LOG_WARN("NodeInfo direct response dropped: no packet buffer");
        return false;
    }

    reply->decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    reply->decoded.payload.size =
        pb_encode_to_bytes(reply->decoded.payload.bytes, sizeof(reply->decoded.payload.bytes), &meshtastic_User_msg, &cachedUser);
    reply->decoded.want_response = false;

    // Start from cached bitfield metadata when available. This lets direct
    // responses preserve more of the original packet semantics (PSRAM path),
    // while still enforcing local policy for OK_TO_MQTT below.
    if (cachedHasDecodedBitfield)
        reply->decoded.bitfield = cachedDecodedBitfield;
    else
        reply->decoded.bitfield = 0;

    // Respect the node-wide config_ok_to_mqtt setting for direct NodeInfo replies.
    // This response is spoofed from another node, so Router::perhapsEncode()
    // will not auto-populate the bitfield via config_ok_to_mqtt for us.
    reply->decoded.has_bitfield = true;
    // Update only the OK_TO_MQTT bit; keep any other cached bits intact.
    reply->decoded.bitfield &= ~BITFIELD_OK_TO_MQTT_MASK;
    if (config.lora.config_ok_to_mqtt)
        reply->decoded.bitfield |= BITFIELD_OK_TO_MQTT_MASK;

    if (hasCachedUser && cachedLastObservedMs != 0) {
        uint32_t ageMs = clockMs() - cachedLastObservedMs;
        TM_LOG_DEBUG("NodeInfo PSRAM hit node=0x%08x age=%lu ms src_ch=%u req_ch=%u rx_time=%lu", p->to,
                     static_cast<unsigned long>(ageMs), static_cast<unsigned>(cachedSourceChannel),
                     static_cast<unsigned>(p->channel), static_cast<unsigned long>(cachedLastObservedRxTime));
    }

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

    // Window ticks: clamp to [1,15] so zero windowMs (config error) opens a new window.
    const uint8_t windowTicks = static_cast<uint8_t>(std::min(static_cast<uint32_t>(15), windowMs / kRateTimeTickMs));
    const uint8_t nowRateTick = currentRateTick();
    const bool windowExpired =
        isNew || entry->getRateCount() == 0 ||
        ((static_cast<uint8_t>(nowRateTick - entry->getRateTime()) & 0x0F) >= std::max(static_cast<uint8_t>(1), windowTicks));
    if (windowExpired) {
        entry->setRateTime(nowRateTick);
        entry->setRateCount(1);
        return false;
    }

    // Increment counter, saturating at 63 (6-bit field max).
    const uint8_t cur = entry->getRateCount();
    if (cur < 0x3F)
        entry->setRateCount(static_cast<uint8_t>(cur + 1));

    // Threshold capped at 60 so a saturated reading (63) always exceeds it.
    uint32_t threshold = moduleConfig.traffic_management.rate_limit_max_packets;
    if (threshold > 60)
        threshold = 60;

    const uint8_t count = entry->getRateCount();
    bool limited = count > threshold;
    if (limited || count == threshold) {
        TM_LOG_DEBUG("Rate limit 0x%08x: count=%u threshold=%u -> %s", from, count, threshold, limited ? "DROP" : "at-limit");
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
    if (moduleConfig.traffic_management.unknown_packet_threshold == 0)
        return false;

    // Fixed 5-tick (5 min) unknown window; capped at 12 ticks (12 min max).
    static constexpr uint8_t kUnknownWindowTicks = 5;

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    // Check if window has expired (presence: getUnknownCount() != 0)
    const uint8_t nowUnknownTick = currentUnknownTick();
    const bool windowExpired = isNew || entry->getUnknownCount() == 0 ||
                               ((static_cast<uint8_t>(nowUnknownTick - entry->getUnknownTime()) & 0x0F) >= kUnknownWindowTicks);
    if (windowExpired) {
        entry->setUnknownTime(nowUnknownTick);
        entry->setUnknownCount(0);
    }

    // Increment counter, saturating at 63 (6-bit field max). With threshold
    // capped at 60, a saturated reading always exceeds the limit - no special
    // already-saturated edge case needed.
    const uint8_t cur = entry->getUnknownCount();
    if (cur < 0x3F)
        entry->setUnknownCount(static_cast<uint8_t>(cur + 1));

    // Threshold capped at 60 so a saturated reading (63) always exceeds it.
    uint32_t threshold = moduleConfig.traffic_management.unknown_packet_threshold;
    if (threshold > 60)
        threshold = 60;

    const uint8_t count = entry->getUnknownCount();
    bool drop = count > threshold;
    if (drop || count == threshold) {
        TM_LOG_DEBUG("Unknown packets 0x%08x: count=%u threshold=%u -> %s", p->from, count, threshold,
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
