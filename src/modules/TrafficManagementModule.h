#pragma once

#include "MeshModule.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#if HAS_TRAFFIC_MANAGEMENT

/**
 * TrafficManagementModule - Packet inspection and traffic shaping for mesh networks.
 *
 * This module provides:
 * - Position deduplication (drop redundant position broadcasts)
 * - Per-node rate limiting (throttle chatty nodes)
 * - Unknown packet filtering (drop undecoded packets from repeat offenders)
 * - NodeInfo direct response (answer queries from cache to reduce mesh chatter)
 * - Local-only telemetry/position (exhaust hop_limit for local broadcasts)
 * - Router hop preservation (maintain hop_limit for router-to-router traffic)
 *
 * Memory Optimization:
 * Uses one flat unified cache (plain array, linear scan) shared by all
 * per-node features instead of separate per-feature caches. Timestamps are
 * stored as free-running modular tick counters (pos: 8-bit 360 s/tick;
 * rate+unknown: paired 4-bit nibbles in one byte) for a 10-byte entry.
 * LoRa packet rates are low enough that an O(n) scan of ~1000 entries is
 * negligible next to packet processing.
 */
class TrafficManagementModule : public MeshModule, private concurrency::OSThread
{
  public:
    TrafficManagementModule();
    ~TrafficManagementModule();

    // Singleton - no copying or moving
    TrafficManagementModule(const TrafficManagementModule &) = delete;
    TrafficManagementModule &operator=(const TrafficManagementModule &) = delete;

    meshtastic_TrafficManagementStats getStats() const;
    void resetStats();
    void recordRouterHopPreserved();

    // Next-hop overflow cache (routing hint).
    // setNextHop: store a confirmed last-byte next hop for `dest`. Called by
    //   NextHopRouter from its ACK-confirmed decision (see sniffReceived). The
    //   byte must come from a bidirectionally-verified relay, not one-way inference.
    // getNextHopHint: return the cached next-hop byte for `dest`, 0 if unknown.
    // clearNextHop: forget any cached next hop for `dest` (setNextHop refuses to store
    //   0, so this is the way NextHopRouter decays a stale/failing overflow route).
    void setNextHop(NodeNum dest, uint8_t nextHopByte);
    uint8_t getNextHopHint(NodeNum dest);
    void clearNextHop(NodeNum dest);

    // Warm-start the next-hop cache from persisted NodeInfoLite hints so confirmed
    // hops survive later hot-store (NodeDB) eviction. Idempotent; runs once after
    // nodeDB is populated (lazily on first maintenance pass).
    // @return true if it actually ran (prereqs met / nothing to do); false if
    //   prerequisites (cache, nodeDB) weren't ready yet, so the caller should retry.
    bool preloadNextHopsFromNodeDB();

    /**
     * Check if this packet should have its hops exhausted.
     * Called from perhapsRebroadcast() to force hop_limit = 0 regardless of
     * router_preserve_hops or favorite node logic.
     */
    bool shouldExhaustHops(const meshtastic_MeshPacket &mp) const
    {
        return exhaustRequested && exhaustRequestedFrom == getFrom(&mp) && exhaustRequestedId == mp.id;
    }

    // Injectable monotonic clock (ms). All TMM time reads go through clockMs() so unit tests can
    // advance a virtual timebase instead of sleeping real seconds across the 6 min/360 s tick.
    // Mirrors HopScalingModule::s_testNowMs. Writable from tests as TrafficManagementModule::s_testNowMs;
    // ignored in production (clockMs() returns millis()).
    inline static uint32_t s_testNowMs = 0;
#ifdef PIO_UNIT_TESTING
    static uint32_t clockMs() { return s_testNowMs; }
#else
    static uint32_t clockMs() { return millis(); }
#endif

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    void alterReceived(meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;
    // Protected so test shims can flush per-node traffic state.
    void flushCache();
    // Introspection for tests: the cached device role for a node, or -1 if the node has
    // no cache entry (distinguishes "not tracked / evicted" from CLIENT == 0).
    int peekCachedRole(NodeNum node);

  private:
    // =========================================================================
    // Unified Cache Entry (10 bytes) - Same for ALL platforms
    // =========================================================================
    //
    // Layout:
    //   [0-3]   node               - NodeNum (4 bytes, 0 = empty slot)
    //   [4]     pos_fingerprint    - 4 bits lat + 4 bits lon (0 = no position seen)
    //   [5]     rate_count         - [7:6] role[3:2] | [5:0] packets in rate window (0 = no window active)
    //   [6]     unknown_count      - [7:6] role[1:0] | [5:0] unknown packets in window (0 = no window active)
    //   [7]     pos_time           - Position tick (uint8, free-running 360 s/tick)
    //   [8]     rate_unknown_time  - [7:4] rate nibble (300 s/tick) | [3:0] unknown nibble (60 s/tick)
    //   [9]     next_hop           - Last-byte relay to reach `node` (0 = none)
    //
    // The 4-bit device role (bits [7:6] of rate_count paired with [7:6] of unknown_count)
    // caches the sender's meshtastic_Config_DeviceConfig_Role as a third fallback after the
    // hot store and warm store, for nodes evicted from both. Read/written via
    // resolveSenderRole(). Max encodable value is 15.
    //
    // Presence sentinels (no epoch, no +1 offset needed):
    //   pos active:     pos_fingerprint != 0
    //   rate active:    getRateCount() != 0     (low 6 bits only)
    //   unknown active: getUnknownCount() != 0  (low 6 bits only)
    //
    // next_hop: routing hint written only from ACK-confirmed NextHopRouter decisions.
    // No TTL - keeps the slot alive across maintenance sweeps.
    //
#if _meshtastic_Config_DeviceConfig_Role_MAX > 15
#warning "Device role enum max exceeds 15 - TMM 4-bit role cache (rate_count[7:6]/unknown_count[7:6]) will truncate new values"
#endif
    struct __attribute__((packed)) UnifiedCacheEntry {
        NodeNum node;
        uint8_t pos_fingerprint;
        uint8_t rate_count;    // [7:6] = role[3:2], [5:0] = count (max 63)
        uint8_t unknown_count; // [7:6] = role[1:0], [5:0] = count (max 63)
        uint8_t pos_time;
        uint8_t rate_unknown_time;
        uint8_t next_hop;

        uint8_t getRateCount() const { return rate_count & 0x3F; }
        void setRateCount(uint8_t c) { rate_count = static_cast<uint8_t>((rate_count & 0xC0) | (c & 0x3F)); }
        uint8_t getUnknownCount() const { return unknown_count & 0x3F; }
        void setUnknownCount(uint8_t c) { unknown_count = static_cast<uint8_t>((unknown_count & 0xC0) | (c & 0x3F)); }
        uint8_t getCachedRole() const { return static_cast<uint8_t>(((rate_count >> 6) << 2) | (unknown_count >> 6)); }
        void setCachedRole(uint8_t role)
        {
            rate_count = static_cast<uint8_t>((rate_count & 0x3F) | ((role >> 2) << 6));
            unknown_count = static_cast<uint8_t>((unknown_count & 0x3F) | ((role & 0x03) << 6));
        }
        uint8_t getRateTime() const { return (rate_unknown_time >> 4) & 0x0F; }
        uint8_t getUnknownTime() const { return rate_unknown_time & 0x0F; }
        void setRateTime(uint8_t t) { rate_unknown_time = static_cast<uint8_t>((rate_unknown_time & 0x0F) | ((t & 0x0F) << 4)); }
        void setUnknownTime(uint8_t t) { rate_unknown_time = static_cast<uint8_t>((rate_unknown_time & 0xF0) | (t & 0x0F)); }
    };
    static_assert(sizeof(UnifiedCacheEntry) == 10, "UnifiedCacheEntry should be 10 bytes");

    // =========================================================================
    // Flat unified cache
    // =========================================================================
    //
    // Plain array, linear scan (same idiom as WarmNodeStore). A lookup walks at
    // most cacheSize() × 10 B - microseconds at LoRa packet rates, not worth a
    // hash table. Insertion on a full cache evicts the stalest entry,
    // preferring entries without a next_hop hint (those are the long-tail
    // routing state this cache exists to keep).
    //
    static constexpr uint16_t cacheSize() { return TRAFFIC_MANAGEMENT_CACHE_SIZE; }

    // NodeInfo cache configuration (PSRAM path): a flat PSRAM array of payload
    // entries, linear scan keyed by `node`, LRU eviction by lastObservedMs.
    // NodeInfo traffic is low-rate, so a full scan per lookup/insert is fine.
    static constexpr uint16_t kNodeInfoCacheEntries = 2000;
    static constexpr uint16_t nodeInfoTargetEntries() { return kNodeInfoCacheEntries; }

    // =========================================================================
    // Free-Running Tick Counters
    // =========================================================================
    //
    // Timestamps are stored as free-running modular tick counters derived from
    // millis(). No epoch anchor needed: modular subtraction gives correct age
    // as long as the true age stays below the counter period.
    //
    //   pos_time  : uint8  (256 ticks × 360 s = 25.6 h period; max window 12 h = 120 ticks)
    //   rate_time : nibble (16 ticks × 300 s = 80 min period; max window 1 h = 12 ticks)
    //   unknown_time: nibble (16 ticks × 60 s = 16 min period; max window 12 min = 12 ticks)
    //
    // Presence sentinels (no +1 offset needed; count fields serve as guards):
    //   pos active:     pos_fingerprint != 0  (0 is reserved sentinel; computePositionFingerprint() remaps computed-0 → 0xFF)
    //   rate active:    getRateCount() != 0   (low 6 bits; high 2 bits are cached role)
    //   unknown active: getUnknownCount() != 0
    //
    static constexpr uint32_t kPosTimeTickMs = 360'000UL;    // 6 min/tick
    static constexpr uint32_t kRateTimeTickMs = 300'000UL;   // 5 min/tick
    static constexpr uint32_t kUnknownTimeTickMs = 60'000UL; // 1 min/tick

    static uint8_t currentPosTick() { return static_cast<uint8_t>(clockMs() / kPosTimeTickMs); }
    static uint8_t currentRateTick() { return static_cast<uint8_t>((clockMs() / kRateTimeTickMs) & 0x0F); }
    static uint8_t currentUnknownTick() { return static_cast<uint8_t>((clockMs() / kUnknownTimeTickMs) & 0x0F); }
    // =========================================================================
    // Position Fingerprint
    // =========================================================================
    //
    // Computes 8-bit fingerprint from truncated lat/lon coordinates.
    // Extracts lower 4 significant bits from each coordinate.
    //
    // fingerprint = (lat_low4 << 4) | lon_low4
    //
    // Unlike a hash, adjacent grid cells have sequential fingerprints,
    // so nearby positions never collide. Collisions only occur for
    // positions 16+ grid cells apart in both dimensions.
    //
    // Guards: If precision < 4 bits, uses min(precision, 4) bits.
    //
    static uint8_t computePositionFingerprint(int32_t lat_truncated, int32_t lon_truncated, uint8_t precision);

    // =========================================================================
    // Cache Storage
    // =========================================================================

    mutable concurrency::Lock cacheLock; // Protects all cache access
    UnifiedCacheEntry *cache = nullptr;  // Flat unified cache (linear scan; all platforms)
    bool cacheFromPsram = false;         // Tracks allocator for correct deallocation

    struct NodeInfoPayloadEntry {
        // Node identifier associated with this payload slot.
        // 0 means the slot is currently unused.
        NodeNum node;

        // Cached NODEINFO_APP payload body. This is separate from NodeDB and is only
        // used by the PSRAM-backed direct-response path in this module.
        meshtastic_User user;

        // Extra response metadata captured from the latest observed NODEINFO_APP
        // packet for this node. shouldRespondToNodeInfo() uses this metadata when
        // building spoofed replies for requesting clients.

        // Last local uptime tick (millis) when this entry was refreshed.
        uint32_t lastObservedMs;

        // Last RTC/packet timestamp (seconds) observed for this NodeInfo frame.
        // If unavailable in packet, remains 0.
        uint32_t lastObservedRxTime;

        // Channel where we most recently heard this node's NodeInfo.
        uint8_t sourceChannel;

        // Cached decoded bitfield metadata from the source packet.
        // We preserve non-OK_TO_MQTT bits in direct replies when available.
        bool hasDecodedBitfield;
        uint8_t decodedBitfield;
    };

    NodeInfoPayloadEntry *nodeInfoPayload = nullptr; // NodeInfo payloads in PSRAM (flat array, linear scan)
    bool nodeInfoPayloadFromPsram = false;           // Tracks allocator for correct deallocation

    meshtastic_TrafficManagementStats stats;

    // Flag set during alterReceived() when packet should be exhausted.
    // Checked by perhapsRebroadcast() to force hop_limit = 0 only for the
    // matching packet key (from + id). Reset at start of handleReceived().
    bool exhaustRequested = false;
    NodeNum exhaustRequestedFrom = 0;
    PacketId exhaustRequestedId = 0;

    // One-shot guard: warm-start next-hop cache from NodeDB on first maintenance pass.
    bool nextHopPreloaded = false;

    // =========================================================================
    // Cache Operations
    // =========================================================================

    // Find or create entry for node (linear scan; stalest-first eviction when full)
    UnifiedCacheEntry *findOrCreateEntry(NodeNum node, bool *isNew);

    // Find existing entry (no creation)
    UnifiedCacheEntry *findEntry(NodeNum node);

    // Resolve a sender's advertised device role for the position hot path. The tier-3
    // cache (this entry's getCachedRole) is authoritative and is kept fresh by
    // updateCachedRoleFromNodeInfo() - updated when NodeDB learns a role, not re-derived
    // per packet. Only on first tracking (isNew) do we scan NodeDB (hot store → warm
    // store, via getNodeRole) to seed the cache, so a resident special-role node is
    // correct from its first position; after that the read is O(1) and survives the node
    // aging out of both NodeDB stores. Caller must hold cacheLock; entry may be null
    // (→ NodeDB scan only).
    meshtastic_Config_DeviceConfig_Role resolveSenderRole(NodeNum from, UnifiedCacheEntry *entry, bool isNew);

    // Refresh the tier-3 role cache from an observed NodeInfo (the same event that updates
    // NodeDB's role). Reads role from the packet's User payload; updates only nodes already
    // tracked (no entry creation). Takes cacheLock.
    void updateCachedRoleFromNodeInfo(const meshtastic_MeshPacket &mp);

    // NodeInfo cache operations (flat PSRAM payload array, linear scan)
    const NodeInfoPayloadEntry *findNodeInfoEntry(NodeNum node) const;
    NodeInfoPayloadEntry *findOrCreateNodeInfoEntry(NodeNum node, bool *usedEmptySlot);
    uint16_t countNodeInfoEntriesLocked() const;
    void cacheNodeInfoPacket(const meshtastic_MeshPacket &mp);

    // =========================================================================
    // Traffic Management Logic
    // =========================================================================

    bool shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs);
    bool shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse);
    bool isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const;
    bool isRateLimited(NodeNum from, uint32_t nowMs);
    bool shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs);

    void logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const;
    void incrementStat(uint32_t *field);
};

static_assert(TRAFFIC_MANAGEMENT_CACHE_SIZE <= UINT16_MAX, "cacheSize() returns uint16_t");

extern TrafficManagementModule *trafficManagementModule;

#endif
