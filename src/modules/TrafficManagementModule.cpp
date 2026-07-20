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

// NodeInfo direct response: role-enforced hop ceilings (respond when hopsAway <= threshold);
// config can only tighten them. nodeinfo_direct_response must also be enabled.
constexpr uint32_t kRouterDefaultMaxHops = 3; // Routers: max 3 hops (can set lower via config)
constexpr uint32_t kClientDefaultMaxHops = 0; // Clients: direct only (cannot increase)

// Staleness window: never spoof a reply for a node not actually heard within it, or a cached
// entry would be served indefinitely for a long-gone node while the genuine request is
// suppressed. The cache path enforces the same 6 h in ticks (kNodeInfoMaxServeAgeTicks, header).
constexpr uint32_t kNodeInfoMaxServeAgeSecs = 6UL * 60UL * 60UL; // 6 h (NodeDB fallback path)

/// Convert seconds to milliseconds with overflow protection.
uint32_t secsToMs(uint32_t secs)
{
    uint64_t milliseconds = static_cast<uint64_t>(secs) * 1000ULL;
    if (milliseconds > UINT32_MAX)
        return UINT32_MAX;
    return static_cast<uint32_t>(milliseconds);
}

/// Advertised role of the originating node, resolved hot store -> warm tier -> CLIENT, so the
/// position dedup role exceptions keep firing for nodes aged out of the hot store.
meshtastic_Config_DeviceConfig_Role originRole(NodeNum from)
{
    return nodeDB ? nodeDB->getNodeRole(from) : meshtastic_Config_DeviceConfig_Role_CLIENT;
}

/// Clamp precision to a valid dedup range.
/// Invalid values use the module default precision.
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

/// Saturating increment for uint8_t counters.
/// Prevents overflow by capping at UINT8_MAX (255).
inline void saturatingIncrement(uint8_t &counter)
{
    if (counter < UINT8_MAX)
        counter++;
}

/// Return a short human-readable name for common port numbers.
/// Falls back to "port:<N>" for unknown ports.
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

/// Allocate the unified cache (and, where available, the NodeInfo cache) and start the sweep.
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

#if TMM_HAS_NODEINFO_CACHE
    TM_LOG_INFO("Allocating NodeInfo cache: %u entries, %u bytes (flat array)", static_cast<unsigned>(nodeInfoTargetEntries()),
                static_cast<unsigned>(nodeInfoTargetEntries() * sizeof(NodeInfoPayloadEntry)));

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    // Production home of this cache: ESP32 PSRAM. No heap fallback here - at 2000 entries the
    // array is too large for MCU internal RAM, so a PSRAM failure disables the cache instead.
    nodeInfoPayload = static_cast<NodeInfoPayloadEntry *>(ps_calloc(nodeInfoTargetEntries(), sizeof(NodeInfoPayloadEntry)));
    if (nodeInfoPayload) {
        nodeInfoPayloadFromPsram = true;
        TM_LOG_INFO("NodeInfo PSRAM cache ready");
    } else {
        TM_LOG_WARN("NodeInfo PSRAM payload allocation failed; direct responses will fall back to NodeDB");
    }
#else
    // Native unit-test build (see TMM_HAS_NODEINFO_CACHE): plain heap, so the cache paths
    // run in CI. nodeInfoPayloadFromPsram stays false and the destructor uses delete[].
    nodeInfoPayload = new NodeInfoPayloadEntry[nodeInfoTargetEntries()]();
#endif
    memaudit::set("tmm_ni", nodeInfoPayload ? nodeInfoTargetEntries() * sizeof(NodeInfoPayloadEntry) : 0);
#else
    TM_LOG_DEBUG("NodeInfo cache not available on this target");
#endif

    setIntervalFromNow(kMaintenanceIntervalMs);
}

/// Both caches may come from ps_calloc (PSRAM, C allocator) or new[] (heap);
/// each must be released with the matching deallocator.
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

void TrafficManagementModule::incrementStat(uint32_t *field)
{
    concurrency::LockGuard guard(&cacheLock);
    (*field)++;
}

// =============================================================================
// Flat Unified Cache Operations
// =============================================================================

/// Find an existing entry for the given node (linear scan).
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

// The two caches are compile-time independent (TMM_HAS_NODEINFO_CACHE keys on PSRAM or
// native tests, the
// unified cache on a per-variant size that may be overridden to 0), so each is purged under
// its own guard - a build with only one of them must still forget deleted nodes.
void TrafficManagementModule::purgeNode(NodeNum node)
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0 || TMM_HAS_NODEINFO_CACHE
    if (node == 0)
        return;
    concurrency::LockGuard guard(&cacheLock);
    bool purged = false;
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    UnifiedCacheEntry *entry = findEntry(node);
    if (entry) {
        memset(entry, 0, sizeof(UnifiedCacheEntry));
        purged = true;
    }
#endif
    // No NodeInfo-cache guard needed: without the cache this is a no-op stub returning null.
    NodeInfoPayloadEntry *info = findNodeInfoEntryMutable(node);
    if (info) {
        memset(info, 0, sizeof(NodeInfoPayloadEntry));
        purged = true;
    }
    // Log only real purges: removeNodeByNum() calls this for every deletion, including
    // nodes these caches never tracked.
    if (purged)
        TM_LOG_INFO("Purged node 0x%08x from traffic caches", node);
#else
    (void)node;
#endif
}

void TrafficManagementModule::purgeAll()
{
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0 || TMM_HAS_NODEINFO_CACHE
    concurrency::LockGuard guard(&cacheLock);
#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    if (cache)
        memset(cache, 0, static_cast<size_t>(cacheSize()) * sizeof(UnifiedCacheEntry));
#endif
    // nodeInfoPayload stays nullptr on builds without the NodeInfo cache; no guard needed.
    if (nodeInfoPayload)
        memset(nodeInfoPayload, 0, static_cast<size_t>(nodeInfoTargetEntries()) * sizeof(NodeInfoPayloadEntry));
    TM_LOG_INFO("Purged all traffic caches");
#endif
}

/// Sender-role resolution for the position hot path. NodeDB is scanned only when a node is first
/// tracked (seeding the tier-3 cache so a resident special-role node is correct from its first
/// position); thereafter the cached role is authoritative, kept fresh by updateCachedRoleFromNodeInfo().
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

/// Refresh the tier-3 role cache from an observed NodeInfo (the same event that updates NodeDB's
/// role), reading the role straight from the packet's User payload. Only updates nodes already
/// tracked, so NodeInfo from non-position nodes can't pollute the cache.
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

/// Find or create the unified-cache entry for `node`. One linear pass tracks the match, the
/// first empty slot, and the eviction victim: when full, the stalest entry loses, preferring
/// entries without a next-hop hint or special role (the long-tail state this cache retains).
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
        UnifiedCacheEntry &entry = cache[i];
        if (entry.node == node)
            return &entry;
        if (entry.node == 0) {
            if (!empty)
                empty = &entry;
            continue;
        }
        if (empty)
            continue; // an empty slot beats any victim; stop scoring
        // "Preferred" entries are evicted last: a confirmed next-hop hint or a cached
        // special (non-CLIENT) role - the long-tail state this cache exists to retain.
        const bool preferred = entry.next_hop != 0 || entry.getCachedRole() != meshtastic_Config_DeviceConfig_Role_CLIENT;
        // Age in pos-ticks (8-bit modular). Entries with no pos state score as maximally old.
        const uint8_t nowPosTick = currentPosTick();
        const uint8_t posAge = static_cast<uint8_t>(nowPosTick - entry.pos_time);
        // Blend in rate/unknown ages scaled to pos-tick units (coarser = conservative).
        const uint8_t rateAgePosScale =
            static_cast<uint8_t>(static_cast<uint8_t>((currentRateTick() - entry.getRateTime()) & 0x0F) * 5 / 3);
        const uint8_t unknownAgePosScale =
            static_cast<uint8_t>(static_cast<uint8_t>((currentUnknownTick() - entry.getUnknownTime()) & 0x0F) / 6);
        uint8_t recencyAge = posAge;
        if (entry.getRateCount() != 0 && rateAgePosScale > recencyAge)
            recencyAge = rateAgePosScale;
        if (entry.getUnknownCount() != 0 && unknownAgePosScale > recencyAge)
            recencyAge = unknownAgePosScale;
        const uint8_t recency = static_cast<uint8_t>(UINT8_MAX - recencyAge);
        if (!victim || (preferred == leastPreferredVictim ? recency < victimRecency : !preferred)) {
            victim = &entry;
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

// =============================================================================
// NodeInfo Payload Cache
// =============================================================================
// One region for every NodeInfo-cache-only function; the #else block at the end
// provides the no-op stubs, so call sites need no guards of their own. Inner
// guards below are only for orthogonal features (PKI, warm tier).
#if TMM_HAS_NODEINFO_CACHE

const TrafficManagementModule::NodeInfoPayloadEntry *TrafficManagementModule::findNodeInfoEntry(NodeNum node) const
{
    if (!nodeInfoPayload || node == 0)
        return nullptr;

    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        if (nodeInfoPayload[i].node == node)
            return &nodeInfoPayload[i];
    }
    return nullptr;
}

/// Find or create a NodeInfo payload entry. Victim selection is trust-tiered so the cache
/// doubles as a pubkey pool: NodeDB membership outranks key trust, then keyless < TOFU key <
/// signer-proven key; within a tier the oldest observation loses (never-observed = oldest).
TrafficManagementModule::NodeInfoPayloadEntry *
TrafficManagementModule::findOrCreateNodeInfoEntry(NodeNum node, bool *usedEmptySlot, bool spareMembers)
{
    if (usedEmptySlot)
        *usedEmptySlot = false;

    if (!nodeInfoPayload || node == 0)
        return nullptr;

    NodeInfoPayloadEntry *empty = nullptr;
    NodeInfoPayloadEntry *victim = nullptr;
    uint8_t victimTier = 0xFF;
    uint8_t victimAge = 0;
    const uint8_t nowObs = currentObsTick();

    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        NodeInfoPayloadEntry &entry = nodeInfoPayload[i];
        if (entry.node == node)
            return &entry;
        if (entry.node == 0) {
            if (!empty)
                empty = &entry;
            continue;
        }
        if (empty)
            continue; // an empty slot beats any victim; stop scoring
        // Eviction tier (lower loses first): 0 keyless, 1 TOFU key, 2 signer-proven key;
        // +3 for NodeDB members - never shed a NodeDB-tier identity over a stranger.
        const uint8_t tier = static_cast<uint8_t>(((entry.user.public_key.size != 32) ? 0 : (entry.keySignerProven ? 2 : 1)) +
                                                  (entry.isMember ? 3 : 0));
        // Modular observation age; saturation keeps real ages far below the 0xFF a
        // never-observed entry scores, so that entry is always the oldest in its tier.
        const uint8_t age = entry.hasObserved ? static_cast<uint8_t>(nowObs - entry.obsTick) : 0xFF;
        if (!victim || tier < victimTier || (tier == victimTier && age > victimAge)) {
            victim = &entry;
            victimTier = tier;
            victimAge = age;
        }
    }

    NodeInfoPayloadEntry *slot = empty ? empty : victim;
    if (!slot)
        return nullptr;
    if (spareMembers && slot == victim && victim->isMember)
        return nullptr; // caller would rather skip than churn one member out for another
    memset(slot, 0, sizeof(NodeInfoPayloadEntry));
    slot->node = node;
    if (usedEmptySlot)
        *usedEmptySlot = (slot == empty);
    return slot;
}

uint16_t TrafficManagementModule::countNodeInfoEntriesLocked() const
{
    if (!nodeInfoPayload)
        return 0;

    uint16_t count = 0;
    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        if (nodeInfoPayload[i].node != 0)
            count++;
    }
    return count;
}

void TrafficManagementModule::reconcileNodeInfoFromNodeDBLocked()
{
    if (!nodeInfoPayload || !nodeDB)
        return;

    const NodeNum self = nodeDB->getNodeNum();
    uint16_t seeded = 0;

    // Upsert one NodeDB-tier node (`hot` non-null = full identity, warm record = key-only).
    // A miss scans the whole array, so this pass is O(members x entries) - which is why it
    // runs hourly plus one boot seed; the write-through hooks carry the interim.
    auto reconcileOne = [&](NodeNum node, const uint8_t *key32, bool signerKnown, const meshtastic_NodeInfoLite *hot) {
        if (node == 0 || node == self)
            return;
        if (!hot && !key32)
            return; // a warm record without a key has nothing worth seeding

        NodeInfoPayloadEntry *entry = findNodeInfoEntryMutable(node);
        if (!entry) {
            bool usedEmptySlot = false;
            entry = findOrCreateNodeInfoEntry(node, &usedEmptySlot, /*spareMembers=*/true);
            if (!entry)
                return; // cache is member-saturated; skip rather than churn a member out
            seeded++;
        }

        // NodeDB is the authority on identity content: adopt its User payload and key. A key
        // change against a stale TMM TOFU pin resets provenance (the signer verdict transfers
        // only key-matched, below). hasObserved/obsTick stay untouched: seeding is not observation.
        const bool keyChanged =
            entry->user.public_key.size == 32 && key32 && memcmp(entry->user.public_key.bytes, key32, 32) != 0;
        if (hot && nodeInfoLiteHasUser(hot)) {
            meshtastic_User merged = TypeConversions::ConvertToUser(hot);
            // Same rule as onNodeIdentityCommitted: a keyless hot identity must not cost this
            // cache a TOFU key it already learned (the kept key stays unproven).
            if (merged.public_key.size != 32 && entry->user.public_key.size == 32)
                merged.public_key = entry->user.public_key;
            entry->user = merged;
            entry->hasFullUser = true;
            snprintf(entry->user.id, sizeof(entry->user.id), "!%08x", node);
        } else if (key32) {
            memcpy(entry->user.public_key.bytes, key32, 32);
            entry->user.public_key.size = 32;
        }
        if (keyChanged)
            entry->keySignerProven = false;
        if (signerKnown && key32 && entry->user.public_key.size == 32 && memcmp(entry->user.public_key.bytes, key32, 32) == 0)
            entry->keySignerProven = true;
        entry->isMember = true;
    };

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        const meshtastic_NodeInfoLite *info = nodeDB->getMeshNodeByIndex(i);
        if (!info || info->num == 0)
            continue;
        const uint8_t *hotKey = (info->public_key.size == 32) ? info->public_key.bytes : nullptr;
        reconcileOne(info->num, hotKey, nodeInfoLiteHasXeddsaSigned(info), info);
    }
#if WARM_NODE_COUNT > 0
    // Warm tier: key-only records (the warm tier keeps no names), so the cache holds a key
    // for every NodeDB identity - the superset the pubkey pool and the key pin rely on.
    for (size_t i = 0; i < nodeDB->warmStore.capacity(); i++) {
        const WarmNodeEntry *warm = nodeDB->warmStore.entryAt(i);
        if (!warm)
            continue;
        const bool hasKey = !memfll(warm->public_key, 0, sizeof(warm->public_key));
        reconcileOne(warm->num, hasKey ? warm->public_key : nullptr, warmSignerOf(*warm), nullptr);
    }
#endif

    // Membership refresh (owned by this hourly pass; per-minute per-entry NodeDB lookups in
    // the sweep were O(entries x members) under cacheLock). Runs AFTER seeding so the upsert
    // pass still sees last pass's bits for its spareMembers protection, then: clear all bits,
    // re-mark from both tiers. Keyless warm records mark membership here even though they had
    // nothing to seed. isMember may now lag a passive NodeDB eviction by up to an hour (the
    // entry just stays LRU-sticky slightly longer); the write-through hooks keep additions
    // immediate, and explicit removals stay immediate via purgeNode().
    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++)
        nodeInfoPayload[i].isMember = false;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        const meshtastic_NodeInfoLite *info = nodeDB->getMeshNodeByIndex(i);
        if (!info || info->num == 0)
            continue;
        NodeInfoPayloadEntry *entry = findNodeInfoEntryMutable(info->num);
        if (entry)
            entry->isMember = true;
    }
#if WARM_NODE_COUNT > 0
    for (size_t i = 0; i < nodeDB->warmStore.capacity(); i++) {
        const WarmNodeEntry *warm = nodeDB->warmStore.entryAt(i);
        if (!warm)
            continue;
        NodeInfoPayloadEntry *entry = findNodeInfoEntryMutable(warm->num);
        if (entry)
            entry->isMember = true;
    }
#endif

    if (seeded)
        TM_LOG_INFO("NodeInfo cache reconciled: %u seeded from NodeDB, %u/%u total", static_cast<unsigned>(seeded),
                    static_cast<unsigned>(countNodeInfoEntriesLocked()), static_cast<unsigned>(nodeInfoTargetEntries()));
}

void TrafficManagementModule::maintainNodeInfoCacheLocked()
{
    if (!nodeInfoPayload)
        return;

    // Saturate expired tick stamps: clearing the presence bit once a stamp's age exceeds
    // its window is the wrap-safety guarantee (stamps never approach their uint8 aliasing
    // horizon). Entries are never freed on a timer - slots die by tiered LRU or purge.
    uint16_t nodeInfoSaturated = 0;
    const uint8_t nowObs = currentObsTick();
    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        NodeInfoPayloadEntry &entry = nodeInfoPayload[i];
        if (entry.node == 0)
            continue;
        if (entry.hasObserved && static_cast<uint8_t>(nowObs - entry.obsTick) > kNodeInfoMaxServeAgeTicks) {
            entry.hasObserved = false;
            nodeInfoSaturated++;
        }
        // Membership is NOT refreshed here: a per-entry NodeDB lookup would be
        // O(entries x members) every 60 s under cacheLock. The hourly reconcile pass
        // owns it (see reconcileNodeInfoFromNodeDBLocked).
    }
    TM_LOG_DEBUG("NodeInfo cache: %u/%u (%u went stale)", static_cast<unsigned>(countNodeInfoEntriesLocked()),
                 static_cast<unsigned>(nodeInfoTargetEntries()), static_cast<unsigned>(nodeInfoSaturated));

    // Anti-entropy: seed identities NodeDB knows but this cache lacks - a full pass at
    // boot (once nodeDB is ready), then hourly. The write-through hooks provide
    // immediacy between passes.
    if (!nodeInfoSeeded || ++sweepsSinceNodeInfoReconcile >= kNodeInfoReconcileSweeps) {
        if (nodeDB) {
            reconcileNodeInfoFromNodeDBLocked();
            nodeInfoSeeded = true;
            sweepsSinceNodeInfoReconcile = 0;
        }
    }
}

void TrafficManagementModule::onNodeIdentityCommitted(NodeNum node, const meshtastic_User &user, bool signerKnown)
{
    // Same gate as handleReceived()/runOnce(): content and maintenance stay keyed to the
    // same condition. Defensive - the object is only constructed while the flag is set
    // (Modules.cpp) and no live path clears it (AdminModule only writes it true; config
    // changes reboot), so a disabled module never reaches these hooks with a live cache.
    if (!moduleConfig.has_traffic_management)
        return;
    if (node == 0 || (nodeDB && node == nodeDB->getNodeNum()))
        return;
    concurrency::LockGuard guard(&cacheLock);
    if (!nodeInfoPayload)
        return;
    bool usedEmptySlot = false;
    NodeInfoPayloadEntry *entry = findOrCreateNodeInfoEntry(node, &usedEmptySlot, /*spareMembers=*/true);
    if (!entry)
        return; // member-saturated cache: the reconcile sweep owns the tradeoff

    // Merge: NodeDB's commit is authoritative for everything it carries, but a keyless
    // commit (possible while the node is unpinned in NodeDB) must not cost this cache a
    // TOFU key it already learned.
    meshtastic_User merged = user;
    if (merged.public_key.size != 32 && !usedEmptySlot && entry->user.public_key.size == 32)
        merged.public_key = entry->user.public_key;

    // Provenance survives only alongside an unchanged key; a replaced key starts from scratch
    // and may be re-proven by signerKnown below - which vouches for the COMMITTED key only.
    const bool sameKey = !usedEmptySlot && entry->user.public_key.size == 32 && merged.public_key.size == 32 &&
                         memcmp(entry->user.public_key.bytes, merged.public_key.bytes, 32) == 0;
    const bool provenBefore = !usedEmptySlot && entry->keySignerProven && sameKey;

    entry->user = merged;
    snprintf(entry->user.id, sizeof(entry->user.id), "!%08x", node);
    entry->hasFullUser = true;
    entry->keySignerProven = provenBefore || (signerKnown && user.public_key.size == 32);
    entry->isMember = true; // committed via updateUser => it sits in the hot store right now
    // obsTick/hasObserved deliberately untouched: only a heard frame makes a node servable.
}

void TrafficManagementModule::onNodeKeyCommitted(NodeNum node, const uint8_t key32[32], bool proven)
{
    // Same module-disabled gate as onNodeIdentityCommitted (see there for rationale).
    if (!moduleConfig.has_traffic_management)
        return;
    if (node == 0 || !key32 || (nodeDB && node == nodeDB->getNodeNum()))
        return;
    concurrency::LockGuard guard(&cacheLock);
    if (!nodeInfoPayload)
        return;
    bool usedEmptySlot = false;
    NodeInfoPayloadEntry *entry = findOrCreateNodeInfoEntry(node, &usedEmptySlot, /*spareMembers=*/true);
    if (!entry)
        return;

    const bool keyChanged = entry->user.public_key.size == 32 && memcmp(entry->user.public_key.bytes, key32, 32) != 0;
    memcpy(entry->user.public_key.bytes, key32, 32);
    entry->user.public_key.size = 32;
    entry->isMember = true; // the caller just committed it to the hot store
    // A rotated key never inherits the old key's verdict; `proven` (manual verification of
    // exactly this key) is the strongest provenance this cache can carry.
    if (keyChanged)
        entry->keySignerProven = false;
    if (proven)
        entry->keySignerProven = true;
    // hasObserved/obsTick untouched: a key commit is knowledge, not an observation.
}

bool TrafficManagementModule::copyPublicKey(NodeNum node, uint8_t out[32], bool *signerProven) const
{
    // Same enable gate as the write-through hooks and maintenance: a disabled module stops
    // updating and sweeping the cache, so its frozen contents must not keep feeding PKI key
    // resolution either. Enforces the "superset only while enabled" corollary (node_info_stores.md).
    if (!moduleConfig.has_traffic_management)
        return false;
    if (!nodeInfoPayload || node == 0 || !out)
        return false;

    concurrency::LockGuard guard(&cacheLock);
    const NodeInfoPayloadEntry *entry = findNodeInfoEntry(node);
    if (!entry || entry->user.public_key.size != 32)
        return false;

    memcpy(out, entry->user.public_key.bytes, 32);
    if (signerProven)
        *signerProven = entry->keySignerProven;
    return true;
}

bool TrafficManagementModule::copyUser(NodeNum node, meshtastic_User &out, bool *signerProven) const
{
    // Enable gate, as in copyPublicKey(): a disabled module must not feed name rehydration
    // from frozen cache contents once its maintenance/write-through have stopped.
    if (!moduleConfig.has_traffic_management)
        return false;
    if (!nodeInfoPayload || node == 0)
        return false;

    concurrency::LockGuard guard(&cacheLock);
    const NodeInfoPayloadEntry *entry = findNodeInfoEntry(node);
    // Key-only records (seeded from the warm tier, which keeps no names) are not a User:
    // handing one to name-rehydration would stamp HAS_USER onto a nameless node.
    if (!entry || !entry->hasFullUser)
        return false;

    out = entry->user;
    if (signerProven)
        *signerProven = entry->keySignerProven;
    return true;
}

#if !(MESHTASTIC_EXCLUDE_PKI)
/// True iff both are full 32-byte public keys with identical bytes. Single point of truth for
/// the key-hygiene checks; raw ptr+size because User and NodeInfoLite key fields differ in type.
static bool pubKeysEqual(const uint8_t *keyA, size_t sizeA, const uint8_t *keyB, size_t sizeB)
{
    return sizeA == 32 && sizeB == 32 && memcmp(keyA, keyB, 32) == 0;
}
#endif

void TrafficManagementModule::cacheNodeInfoPacket(const meshtastic_MeshPacket &mp)
{
    if (!nodeInfoPayload || mp.decoded.payload.size == 0)
        return;

    meshtastic_User user = meshtastic_User_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &user))
        return;

    const NodeNum from = getFrom(&mp);

    // Normalize user.id to the packet sender's node number.
    snprintf(user.id, sizeof(user.id), "!%08x", from);

    // Whether NodeDB already knows this node as a verified signer for this key. Lets a
    // re-found node inherit proven provenance even when THIS frame happens to be unsigned.
    bool dbSaysSigner = false;

#if !(MESHTASTIC_EXCLUDE_PKI)
    // This cache is served back as spoofed replies, so mirror NodeDB::updateUser()'s key
    // hygiene. First: a frame advertising our own key is impersonating us - never cache it.
    if (pubKeysEqual(user.public_key.bytes, user.public_key.size, owner.public_key.bytes, owner.public_key.size)) {
        TM_LOG_WARN("NodeInfo cache: incoming key matches owner, dropping 0x%08x", from);
        return;
    }
    // Key pin against the authoritative NodeDB key (hot store, then warm tier - the same
    // coverage as updateUser's pin): a hot-only check would let an attacker seed a bogus key
    // for a warm-evicted node, and the TOFU pin below would then lock the genuine node out.
    meshtastic_NodeInfoLite_public_key_t dbKey = {0, {0}};
    if (nodeDB && nodeDB->copyPublicKeyAuthoritative(from, dbKey) &&
        !pubKeysEqual(user.public_key.bytes, user.public_key.size, dbKey.bytes, dbKey.size)) {
        TM_LOG_WARN("NodeInfo cache: public key mismatch vs NodeDB, dropping 0x%08x", from);
        return;
    }
    // Re-found in NodeDB as a known signer: inherit that verdict even off an unsigned frame.
    // isVerifiedSignerForKey covers both tiers and requires the key to match, so a warm-only
    // signer is included and a rotated key never inherits a stale verdict.
    dbSaysSigner = nodeDB && user.public_key.size == 32 && nodeDB->isVerifiedSignerForKey(from, user.public_key.bytes);
#endif

    bool usedEmptySlot = false;
    uint16_t cachedCount = 0;
    {
        concurrency::LockGuard guard(&cacheLock);
        NodeInfoPayloadEntry *entry = findOrCreateNodeInfoEntry(from, &usedEmptySlot);
        if (!entry)
            return;

#if !(MESHTASTIC_EXCLUDE_PKI)
        // Trust-on-first-use pinning within our own cache: if NodeDB has since evicted the
        // node but we already cached a key for it, still refuse a mismatching key. (A matched
        // existing slot is returned un-zeroed, so entry->user holds the previously cached key.)
        if (!usedEmptySlot && entry->node == from && entry->user.public_key.size == 32 &&
            !pubKeysEqual(user.public_key.bytes, user.public_key.size, entry->user.public_key.bytes,
                          entry->user.public_key.size)) {
            TM_LOG_WARN("NodeInfo cache: public key mismatch vs cache, dropping 0x%08x", from);
            return;
        }
#endif

        // Cache payload plus response metadata so direct replies carry richer context than
        // just the User protobuf. Intentionally independent from NodeInfoModule/NodeDB.
        entry->user = user;
        entry->obsTick = currentObsTick(); // a genuine observation - the only place obsTick is stamped
        entry->hasObserved = true;
        entry->hasFullUser = true; // an observed NODEINFO frame always carries the full User
        entry->sourceChannel = mp.channel;
        entry->hasDecodedBitfield = mp.decoded.has_bitfield;
        entry->decodedBitfield = mp.decoded.bitfield;

        // Upgrade to signer-proven on a Router-verified signature or a NodeDB signer verdict
        // for this same key. Never downgrade (a later unsigned frame leaves the flag set),
        // and the key itself cannot change here - the pin checks above already rejected that.
        if ((mp.xeddsa_signed || dbSaysSigner) && user.public_key.size == 32)
            entry->keySignerProven = true;

        if (usedEmptySlot)
            cachedCount = countNodeInfoEntriesLocked();
    }

    if (usedEmptySlot) {
        TM_LOG_INFO("NodeInfo cache entries: %u/%u", static_cast<unsigned>(cachedCount),
                    static_cast<unsigned>(nodeInfoTargetEntries()));
    }
}

void TrafficManagementModule::dropNodeInfoCacheForTest()
{
    concurrency::LockGuard guard(&cacheLock);
    if (!nodeInfoPayload)
        return;
    if (nodeInfoPayloadFromPsram)
        free(nodeInfoPayload);
    else
        delete[] nodeInfoPayload;
    nodeInfoPayload = nullptr;
    nodeInfoPayloadFromPsram = false;
    memaudit::set("tmm_ni", 0);
}

int TrafficManagementModule::peekNodeInfoFlagsForTest(NodeNum node)
{
    concurrency::LockGuard guard(&cacheLock);
    const NodeInfoPayloadEntry *entry = findNodeInfoEntry(node);
    if (!entry)
        return -1;
    return (entry->hasObserved ? 1 : 0) | (entry->isMember ? 2 : 0) | (entry->hasFullUser ? 4 : 0) |
           (entry->keySignerProven ? 8 : 0);
}

void TrafficManagementModule::markKeySignerProvenForTest(NodeNum node)
{
    concurrency::LockGuard guard(&cacheLock);
    if (!nodeInfoPayload)
        return;
    for (uint16_t i = 0; i < nodeInfoTargetEntries(); i++) {
        if (nodeInfoPayload[i].node == node) {
            nodeInfoPayload[i].keySignerProven = true;
            return;
        }
    }
}

#else // !TMM_HAS_NODEINFO_CACHE: no-op stubs so call sites need no guards of their own

const TrafficManagementModule::NodeInfoPayloadEntry *TrafficManagementModule::findNodeInfoEntry(NodeNum) const
{
    return nullptr;
}
TrafficManagementModule::NodeInfoPayloadEntry *TrafficManagementModule::findOrCreateNodeInfoEntry(NodeNum, bool *usedEmptySlot,
                                                                                                  bool)
{
    if (usedEmptySlot)
        *usedEmptySlot = false;
    return nullptr;
}
uint16_t TrafficManagementModule::countNodeInfoEntriesLocked() const
{
    return 0;
}
void TrafficManagementModule::reconcileNodeInfoFromNodeDBLocked() {}
void TrafficManagementModule::maintainNodeInfoCacheLocked() {}
void TrafficManagementModule::onNodeIdentityCommitted(NodeNum, const meshtastic_User &, bool) {}
void TrafficManagementModule::onNodeKeyCommitted(NodeNum, const uint8_t[32], bool) {}
bool TrafficManagementModule::copyPublicKey(NodeNum, uint8_t[32], bool *) const
{
    return false;
}
bool TrafficManagementModule::copyUser(NodeNum, meshtastic_User &, bool *) const
{
    return false;
}
void TrafficManagementModule::cacheNodeInfoPacket(const meshtastic_MeshPacket &) {}
void TrafficManagementModule::dropNodeInfoCacheForTest() {}
int TrafficManagementModule::peekNodeInfoFlagsForTest(NodeNum)
{
    return -1;
}
void TrafficManagementModule::markKeySignerProvenForTest(NodeNum) {}

#endif // TMM_HAS_NODEINFO_CACHE

// =============================================================================
// Next-Hop Overflow Cache
// =============================================================================
// Routing hints (last byte of the next-hop NodeNum), written ONLY from NextHopRouter's
// ACK-confirmed decisions - never inferred one-way. Holds confirmed hops that aged out of
// the hot NodeDB; NextHopRouter::getNextHop() consults it as a fallback after the hot store.

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

/// 8-bit position fingerprint: (lat_low4 << 4) | lon_low4 from the truncated coordinates'
/// lower significant bits. Deterministic, so adjacent grid cells never collide; two positions
/// collide only when 16+ cells apart in BOTH dimensions.
uint8_t TrafficManagementModule::computePositionFingerprint(int32_t lat_truncated, int32_t lon_truncated, uint8_t precision)
{
    precision = sanitizePositionPrecision(precision);

    // With precision < 4 there are fewer significant bits to take.
    uint8_t bitsToTake = (precision < 4) ? precision : 4;

    // Shift the significant bits to the bottom, then mask the low bits of each coordinate.
    uint8_t shift = 32 - precision;
    uint8_t latBits = (static_cast<uint32_t>(lat_truncated) >> shift) & ((1u << bitsToTake) - 1);
    uint8_t lonBits = (static_cast<uint32_t>(lon_truncated) >> shift) & ((1u << bitsToTake) - 1);

    const uint8_t fingerprint = static_cast<uint8_t>((latBits << 4) | lonBits);
    // 0 is the "no position seen" sentinel, so remap a computed 0 -> 0xFF (mirrors
    // getLastByteOfNodeNum). Cost: one extra collision bucket in 256 - negligible.
    return fingerprint ? fingerprint : 0xFF;
}

// =============================================================================
// Packet Handling
// =============================================================================

/// Runs BEFORE RoutingModule in callModules(): STOP fully consumes the packet (no rebroadcast),
/// ignoreRequest suppresses the default NAK for want_response packets, and exhaustRequested
/// (set by alterReceived) makes perhapsRebroadcast() force hop_limit=0 on the relayed copy.
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

    // A known signer's NodeInfo arriving unsigned is unauthenticated and must not drive any
    // cache or identity write below. isKnownXeddsaSigner covers the warm tier too - a forged
    // unsigned NodeInfo carrying a warm-evicted signer's real (public!) key passes the key pin.
    const bool isNodeInfo = mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP;
    const bool unauthenticatedSigner = isNodeInfo && !mp.xeddsa_signed && nodeDB && nodeDB->isKnownXeddsaSigner(getFrom(&mp));

    // Learn NodeInfo payloads into the dedicated NodeInfo cache, and refresh the tier-3
    // role cache for any node we already track (keeps the dedup role exception current).
    if (isNodeInfo && !unauthenticatedSigner) {
        cacheNodeInfoPacket(mp);
        updateCachedRoleFromNodeInfo(mp);
    }

    // -------------------------------------------------------------------------
    // NodeInfo Direct Response
    // -------------------------------------------------------------------------
    // Answer a unicast NodeInfo request for a known node straight from cache: STOP keeps the
    // request from being rebroadcast, and the reply goes back to the requestor with hop_limit=0.

    if (cfg.nodeinfo_direct_response_max_hops > 0 && mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
        mp.decoded.want_response && !isBroadcast(mp.to) && !isToUs(&mp) && !isFromUs(&mp)) {
        if (shouldRespondToNodeInfo(&mp, true)) {
            // Unicast NodeInfo is never signed, so a known signer's identity claim here is
            // unauthenticated: don't overwrite its stored name. The cached response is unaffected.
            meshtastic_User requester = meshtastic_User_init_zero;
            if (!unauthenticatedSigner &&
                pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &requester)) {
                // Re-enters this module: updateUser's write-through hook calls back into
                // onNodeIdentityCommitted, which takes cacheLock - safe here because this
                // call site never holds cacheLock.
                nodeDB->updateUser(getFrom(&mp), requester, mp.channel, mp.xeddsa_signed);
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

    // exhaust_hop_telemetry / exhaust_hop_position / router_preserve_hops: shelved until the
    // right heuristics are clearer; exhaustRequested stays false and rebroadcast is normal.

    const bool isPosition = mp.decoded.portnum == meshtastic_PortNum_POSITION_APP;

    // -------------------------------------------------------------------------
    // Relayed Position Precision Clamp
    // -------------------------------------------------------------------------
    // Never forward more-precise coordinates than the channel is configured to carry
    // (chanPrec==0 = sharing disabled on channel: skip). Ham mode is exempt; lost-and-found
    // is not. Compile USERPREFS_TMM_APPLY_TO_PRIVATE_CHANNELS to extend to private channels.
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
    // Warm-start the next-hop cache once nodeDB has finished loading (hence here, not the
    // constructor; it takes its own lock, so run before the sweep guard). Latch the one-shot
    // guard only when the preload actually ran, so a not-ready nodeDB gets a retry.
    if (!nextHopPreloaded && preloadNextHopsFromNodeDB())
        nextHopPreloaded = true;
#endif

#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0 || TMM_HAS_NODEINFO_CACHE
    // Mirrors purgeAll(): the two caches are compile-time independent (a variant may zero the
    // unified cache while the NodeInfo cache exists, or vice versa), so each is maintained
    // under its own guard below - a build with only one of them must still sweep it.
    concurrency::LockGuard guard(&cacheLock);
#endif

#if TRAFFIC_MANAGEMENT_CACHE_SIZE > 0
    // TTLs in free-running ticks: pos 4x position interval (<=255 @ 6 min/tick), rate 2x the
    // configured window (<=15 @ 5 min/tick), unknown fixed 12 @ 1 min/tick.
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

        // Confirmed next-hop hints and cached special roles have no TTL and pin the slot, so
        // a tracker/router keeps its dedup exception across quiet periods instead of
        // reverting to CLIENT the moment its timed state expires.
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

#endif // TRAFFIC_MANAGEMENT_CACHE_SIZE > 0

    maintainNodeInfoCacheLocked(); // no-op stub on builds without the NodeInfo cache

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
    // Drop gate uses the RAW configured interval: 0 means "dedup disabled". The 12 h default
    // is only for TTL sizing - feeding it here would silently defeat that contract.
    uint32_t minIntervalMs = secsToMs(moduleConfig.traffic_management.position_min_interval_secs);

    bool isNew = false;
    concurrency::LockGuard guard(&cacheLock);
    UnifiedCacheEntry *entry = findOrCreateEntry(p->from, &isNew);
    if (!entry)
        return false;

    // Role exceptions keyed on the sender's advertised role, resolved hot -> warm -> tier-3
    // cache; this path also keeps tier 3 warm so the exception survives NodeDB eviction.
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

    // Extra metadata consumed only by the NodeInfo-cache path.
    // Defaults preserve previous behavior when cache metadata is unavailable.
    bool cachedHasDecodedBitfield = false;
    uint8_t cachedDecodedBitfield = 0;
    uint8_t cachedSourceChannel = 0;
    bool cachedHasObserved = false;
    uint8_t cachedObsTick = 0;
    // Signer-proven provenance of the cached key, consumed by the replay gate below
    // (maybe_unused: read only when TMM_NODEINFO_REPLAY_SIGNED_GATE is compiled in).
    [[maybe_unused]] bool cachedKeySignerProven = false;
    // True once we commit to answering from the NodeDB fallback (no NodeInfo cache) path. The
    // response throttle no longer distinguishes the paths - the per-requester/per-target RAM
    // tables cover both - but the replay gate below still keys off it.
    bool usedFallback = false;

    {
        concurrency::LockGuard guard(&cacheLock);
        const NodeInfoPayloadEntry *entry = findNodeInfoEntry(p->to);
        if (entry) {
            cachedUser = entry->user;
            hasCachedUser = true;
            cachedHasDecodedBitfield = entry->hasDecodedBitfield;
            cachedDecodedBitfield = entry->decodedBitfield;
            cachedSourceChannel = entry->sourceChannel;
            cachedHasObserved = entry->hasObserved;
            cachedObsTick = entry->obsTick;
            cachedKeySignerProven = entry->keySignerProven;
        }
    }

    if (!hasCachedUser) {
        // If the NodeInfo cache exists but misses, we intentionally do not fall back
        // to the node-wide table. This keeps the cache-backed direct-reply path separate
        // from NodeInfoModule/NodeDB behavior when the cache is available.
        if (nodeInfoPayload) {
            TM_LOG_DEBUG("NodeInfo cache miss for node=0x%08x", p->to);
            return false;
        }

        // Fallback only when the NodeInfo cache is unavailable on this target.
        // In this mode we use the node-wide table maintained by NodeInfoModule.
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(p->to);
        if (!nodeInfoLiteHasUser(node))
            return false;
        // Staleness gate (fallback path): don't spoof a reply for a node last heard beyond the
        // serve window. last_heard == 0 means recency is unknown (clock not set) - we can't
        // prove staleness, so still answer. Age computed once so test and log can't diverge.
        const uint32_t nodeAgeSecs = sinceLastSeen(node);
        if (node->last_heard != 0 && nodeAgeSecs > kNodeInfoMaxServeAgeSecs) {
            TM_LOG_DEBUG("NodeInfo NodeDB entry for 0x%08x stale (%us ago), not responding", p->to,
                         static_cast<unsigned>(nodeAgeSecs));
            return false;
        }
#if TMM_NODEINFO_REPLAY_SIGNED_GATE
        // Replay provenance gate (fallback path): only vouch for a node NodeDB knows as a
        // verified signer. An unproven (trust-on-first-use) identity is left for the genuine
        // node or another cache-holder to answer.
        if (!nodeInfoLiteHasXeddsaSigned(node)) {
            TM_LOG_DEBUG("NodeInfo NodeDB entry for 0x%08x not signer-proven, not responding", p->to);
            return false;
        }
#endif
        cachedUser = TypeConversions::ConvertToUser(node);
        usedFallback = true;
    }

    // Staleness gate (cache path): never spoof a reply for a node not actually HEARD within
    // the serve window. hasObserved distinguishes genuinely observed entries from seeded ones,
    // and the sweep's saturation keeps this modular tick compare alias-free.
    if (hasCachedUser &&
        (!cachedHasObserved || static_cast<uint8_t>(currentObsTick() - cachedObsTick) > kNodeInfoMaxServeAgeTicks)) {
        TM_LOG_DEBUG("NodeInfo cache entry for 0x%08x not freshly observed, not responding", p->to);
        return false;
    }

#if TMM_NODEINFO_REPLAY_SIGNED_GATE
    // Replay provenance gate (cache path): only spoof a reply for a signer-proven cached key.
    // usedFallback entries were already gated above. See TMM_NODEINFO_REPLAY_REQUIRE_SIGNED.
    if (!usedFallback && !cachedKeySignerProven) {
        TM_LOG_DEBUG("NodeInfo cache entry for 0x%08x not signer-proven, not responding", p->to);
        return false;
    }
#endif

    if (!sendResponse)
        return true;

    // Direct-response throttle: the reply is addressed to the requesting packet's unauthenticated
    // `from` and spoofs the requested target, and direct responses bypass the rate limiter - so bound
    // them per requester, per target, and by a global airtime floor (see directResponseAllowed). One
    // check covers both the cache and NodeDB-fallback paths, since the throttle state is RAM tables,
    // not per-cache-entry. Checked here, once a reply would actually go out, so requests declined for
    // other reasons do not consume the budget. Returning false lets handleReceived() CONTINUE, so the
    // request forwards toward the genuine target (which can answer) instead of being consumed with no
    // reply; a requester whose first reply was lost on a noisy link would otherwise get silence for
    // the whole window. Repeats of the same packet id are already absorbed by the router's dup detect.
    if (!directResponseAllowed(getFrom(p), p->to, clockMs())) {
        TM_LOG_DEBUG("NodeInfo direct response throttled for 0x%08x; forwarding request instead", getFrom(p));
        return false;
    }

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
    // responses preserve more of the original packet semantics (cache path),
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

    if (hasCachedUser) {
        TM_LOG_DEBUG("NodeInfo cache hit node=0x%08x age=%u obs-ticks src_ch=%u req_ch=%u", p->to,
                     static_cast<unsigned>(static_cast<uint8_t>(currentObsTick() - cachedObsTick)),
                     static_cast<unsigned>(cachedSourceChannel), static_cast<unsigned>(p->channel));
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

    // The throttle state was already recorded by directResponseAllowed() above (it stamps only when
    // it admits a reply), so there is nothing to stamp here.

    return true;
}

TrafficManagementModule::DirectResponseThrottleEntry *
TrafficManagementModule::directResponseSlot(DirectResponseThrottleEntry *table, NodeNum key, uint32_t nowMs, uint32_t windowMs)
{
    // Known key still inside its window -> throttled (nullptr). Outside it -> reuse that slot.
    for (size_t i = 0; i < kDirectResponseTrackedNodes; i++) {
        if (table[i].key == key)
            return ((nowMs - table[i].lastReplyMs) < windowMs) ? nullptr : &table[i];
    }
    // Unseen key: take a free slot, otherwise evict the least recently used one.
    DirectResponseThrottleEntry *slot = &table[0];
    for (size_t i = 0; i < kDirectResponseTrackedNodes; i++) {
        if (table[i].key == 0)
            return &table[i];
        if (table[i].lastReplyMs < slot->lastReplyMs)
            slot = &table[i];
    }
    return slot;
}

bool TrafficManagementModule::directResponseAllowed(NodeNum requester, NodeNum target, uint32_t nowMs)
{
    // Reached from the packet path (and shares state with the cache), so take the same lock.
    concurrency::LockGuard guard(&cacheLock);

    // Global airtime floor first: cheapest, and the backstop once an attacker cycles requester/target
    // past the 8-slot tables.
    if (lastDirectResponseMs != 0 && (nowMs - lastDirectResponseMs) < kDirectResponseGlobalMs)
        return false;

    // Resolve both slots before stamping either, so a reply the target axis throttles does not consume
    // the requester's budget (and vice versa).
    DirectResponseThrottleEntry *reqSlot =
        directResponseSlot(directRequesterSeen, requester, nowMs, kDirectResponsePerRequesterMs);
    if (reqSlot == nullptr)
        return false;
    DirectResponseThrottleEntry *tgtSlot = directResponseSlot(directTargetSeen, target, nowMs, kDirectResponsePerTargetMs);
    if (tgtSlot == nullptr)
        return false;

    // All windows clear: admit the reply and record it on every axis.
    reqSlot->key = requester;
    reqSlot->lastReplyMs = nowMs;
    tgtSlot->key = target;
    tgtSlot->lastReplyMs = nowMs;
    lastDirectResponseMs = nowMs;
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
    const uint8_t currentCount = entry->getRateCount();
    if (currentCount < 0x3F)
        entry->setRateCount(static_cast<uint8_t>(currentCount + 1));

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

    // Increment counter, saturating at 63 (6-bit field max). With the threshold capped at
    // 60, a saturated reading always exceeds the limit - no special edge case needed.
    const uint8_t currentCount = entry->getUnknownCount();
    if (currentCount < 0x3F)
        entry->setUnknownCount(static_cast<uint8_t>(currentCount + 1));

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
