#pragma once

#include "MeshModule.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#if HAS_TRAFFIC_MANAGEMENT

// Replay provenance gate (compile-time). When 1 (default), the NodeInfo direct-response path
// only spoofs a reply on behalf of a node whose cached key is signer-proven - an XEdDSA
// signature was verified against it - in addition to the staleness gate. Replay is a courtesy
// feature, and vouching for an unverified (trust-on-first-use) identity to other nodes is the
// risk this closes, so signed-only is the safer default. Define as 0 at build time to also
// serve fresh TOFU-only nodes (the prior, more permissive behavior). No effect when PKI is
// excluded from the build: nothing can be signed there, so the gate is bypassed to avoid
// disabling the courtesy feature outright.
#ifndef TMM_NODEINFO_REPLAY_REQUIRE_SIGNED
#define TMM_NODEINFO_REPLAY_REQUIRE_SIGNED 1
#endif

// Effective gate: only meaningful when PKI is compiled in.
#if TMM_NODEINFO_REPLAY_REQUIRE_SIGNED && !(MESHTASTIC_EXCLUDE_PKI)
#define TMM_NODEINFO_REPLAY_SIGNED_GATE 1
#else
#define TMM_NODEINFO_REPLAY_SIGNED_GATE 0
#endif

// NodeInfo cache availability (compile-time). The cache's production home is ESP32 boards
// with PSRAM - at 2000 entries the flat array is far too large for MCU internal RAM - but
// the code itself is portable. Native unit-test builds (ARCH_PORTDUINO + PIO_UNIT_TESTING)
// enable it on the plain heap so the cache paths - key pinning, signer provenance,
// staleness, throttle, retention - run in CI instead of only on hardware. Production
// portduino (meshtasticd) and embedded test builds are unaffected. Tests that need the
// NodeDB fallback path instead call dropNodeInfoCacheForTest().
#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || (defined(ARCH_PORTDUINO) && defined(PIO_UNIT_TESTING))
#define TMM_HAS_NODEINFO_CACHE 1
#else
#define TMM_HAS_NODEINFO_CACHE 0
#endif

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

    // Last-resort public-key source for NodeDB::copyPublicKey(). After the hot (NodeInfoLite)
    // and warm (WarmNodeStore) tiers miss, the NodeInfo direct-response cache may still hold a
    // key learned from an observed NODEINFO frame, extending the pool of keys the node can
    // encrypt to. Copies the 32-byte key for `node` into out[32] and returns true if present;
    // false otherwise (including builds without the PSRAM NodeInfo cache). If `signerProven`
    // is non-null, reports whether that key was verified via an XEdDSA signature (true) or is
    // trust-on-first-use (false), so callers can weigh its trust. Thread-safe (takes cacheLock).
    bool copyPublicKey(NodeNum node, uint8_t out[32], bool *signerProven = nullptr) const;

    // Copy the full cached User (name, short name, hw model, role, key, flags) for `node`, if the
    // NodeInfo direct-response cache holds one. Used by NodeDB to rehydrate a re-admitted node's
    // identity - the warm tier keeps a node's key but not its name, and this cache (much larger)
    // often still has it. Returns false on miss or on builds without the PSRAM cache. If
    // `signerProven` is non-null, reports the cached key's provenance. Thread-safe (takes cacheLock).
    bool copyUser(NodeNum node, meshtastic_User &out, bool *signerProven = nullptr) const;

    // Write-through hook from NodeDB: called after updateUser() commits a remote node's
    // identity, so this cache reflects NodeDB immediately instead of waiting for the next
    // reconcile sweep (which remains the anti-entropy backstop). Upserts the full User;
    // NodeDB's key is authoritative (updateUser's own pin already rejected conflicts
    // upstream, so a differing cached key is stale residue and is replaced, dropping its
    // provenance), while a keyless commit keeps a key this cache already holds. When
    // signerKnown (the node's verified-signer bit) and the commit carries a key, the key
    // is marked signer-proven. Never touches the observation stamp: knowledge is not
    // observation, so a hook write can never make a node servable by the replay path.
    // No-op without the cache. Thread-safe (takes cacheLock).
    void onNodeIdentityCommitted(NodeNum node, const meshtastic_User &user, bool signerKnown);

    // Full-removal hook from NodeDB::removeNodeByNum(): an explicit removal must forget the
    // node here too - the unified cache slot (role, next-hop hint, dedup/rate state) and the
    // NodeInfo cache entry (name/key/provenance) - or these caches would keep serving the
    // deleted identity (key pool, rehydration) and resurrect it on next contact. Passive
    // NodeDB eviction never calls this; the reconcile sweep will not re-seed a node that is
    // gone from both NodeDB tiers. Thread-safe (takes cacheLock).
    void purgeNode(NodeNum node);

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

    // Timestamp for the one remaining millis-based field that uses 0 as a "never" sentinel
    // (nodeInfoFallbackLastResponseMs - the cache entries use tick clocks with explicit
    // validity bits instead). clockMs() is 0 for exactly one millisecond every ~49.7-day
    // wrap, which would collide with the sentinel and momentarily disable the fallback
    // throttle for a freshly stamped reply. Map that one instant to 1; the 1 ms skew is
    // irrelevant to the throttle window. (T9)
    static uint32_t nowStampMs()
    {
        const uint32_t t = clockMs();
        return t == 0 ? 1u : t;
    }

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

    // Test hook: force a cached NodeInfo entry's key to signer-proven, so replay-gate tests can
    // exercise the signed-only path without standing up a full XEdDSA verification. No-op if the
    // node is not cached or the NodeInfo cache is absent.
    void markKeySignerProvenForTest(NodeNum node);

    // Test hook: free and clear the NodeInfo cache so the NodeDB fallback direct-response
    // path can be exercised in a build where the cache is compiled in (native test builds
    // allocate it on the heap - see TMM_HAS_NODEINFO_CACHE). No-op when already absent.
    void dropNodeInfoCacheForTest();

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
    // entries, linear scan keyed by `node`, trust/membership-tiered LRU eviction on insert.
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

    // NodeInfo cache ticks (same idiom, applied to NodeInfoPayloadEntry):
    //   obsTick : uint8 (256 ticks x 3 min = 12.8 h period; serve window 6 h = 120 ticks, 2.1x margin)
    //   respTick: uint8 (256 ticks x 5 s = 21.3 min period; throttle window 30 s = 6 ticks, 42x margin)
    // Presence is an explicit bit per stamp (hasObserved / hasResponded), not a 0-sentinel;
    // the 60 s maintenance sweep clears each bit once its window passes ("saturation"), so a
    // stamp is never read anywhere near its aliasing horizon. +-1 tick granularity error
    // (+-3 min on a 6 h gate, +-5 s on a 30 s throttle) is noise for these windows.
    static constexpr uint32_t kNodeInfoObsTickMs = 180000UL; // 3 min/tick
    static constexpr uint32_t kNodeInfoRespTickMs = 5000UL;  // 5 s/tick
    static constexpr uint8_t kNodeInfoMaxServeAgeTicks = 120; // 6 h serve window
    static constexpr uint8_t kNodeInfoThrottleTicks = 6;      // 30 s throttle window

    static uint8_t currentObsTick() { return static_cast<uint8_t>(clockMs() / kNodeInfoObsTickMs); }
    static uint8_t currentRespTick() { return static_cast<uint8_t>(clockMs() / kNodeInfoRespTickMs); }
    static_assert(kNodeInfoMaxServeAgeTicks * kNodeInfoObsTickMs == 6UL * 60UL * 60UL * 1000UL,
                  "cache serve window must equal the fallback path's 6 h");
    static_assert(kNodeInfoThrottleTicks * kNodeInfoRespTickMs == 30UL * 1000UL,
                  "cache throttle window must equal the fallback path's 30 s");
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

        // Free-running tick stamps (uint8 modular clocks, kNodeInfo*TickMs in the header -
        // same scheme as the UnifiedCache tick counters). Validity of obsTick/respTick is
        // carried by the hasObserved/hasResponded flag bits below, not a 0 sentinel; the
        // maintenance sweep clears a flag once its window passes (saturation), so no stamp
        // can age far enough for its modular compare to alias.

        // Free-running tick (kNodeInfoObsTickMs) of the last genuinely HEARD NODEINFO frame
        // from this node. Drives the replay staleness gate and the LRU age - seeding and
        // write-through hooks never touch it, so a spoofed reply is only ever backed by
        // genuine recent observation.
        uint8_t obsTick;

        // Free-running tick (kNodeInfoRespTickMs) of the most recent spoofed direct reply
        // we emitted for this node. Drives the per-target response throttle.
        uint8_t respTick;

        // Channel where we most recently heard this node's NodeInfo.
        uint8_t sourceChannel;

        // Cached decoded bitfield metadata from the source packet.
        // We preserve non-OK_TO_MQTT bits in direct replies when available.
        uint8_t decodedBitfield;

        // Boolean flags, declared adjacent as 1-bit fields so the compiler packs them into a
        // single byte, leaving 6 spare bits for future flags without growing the 2000-entry
        // PSRAM array. Access is by name, exactly like plain bools.

        // The source packet carried a decoded bitfield (so decodedBitfield is meaningful).
        uint8_t hasDecodedBitfield : 1;

        // Provenance of user.public_key: 1 once we have observed a NODEINFO_APP frame for this
        // node whose XEdDSA signature we verified (directly via mp.xeddsa_signed, or inherited
        // from NodeDB via isVerifiedSignerForKey) - i.e. the key is proven to belong to a
        // signer, not merely trust-on-first-use. Monotonic: once set it stays set for the life
        // of the slot (the key-pin checks forbid the key changing underneath it). Used as a
        // trust tier: proven keys are the stickiest under LRU eviction and are reported to
        // copyPublicKey() consumers. A signature can only be verified against a key we already
        // held, so a first-contact key is always TOFU (0) until a later signed frame upgrades it.
        uint8_t keySignerProven : 1;

        // obsTick is valid: we have actually heard a NODEINFO_APP frame from this node
        // within the observation clock's horizon. Cleared by the sweep once the serve
        // window passes (saturation) - both states mean "do not serve a spoofed reply".
        uint8_t hasObserved : 1;

        // respTick is valid: we emitted a spoofed reply within the throttle clock's
        // horizon. Cleared by the sweep once the throttle window passes.
        uint8_t hasResponded : 1;

        // `user` carries a real User payload (name etc.) - from an observed NODEINFO frame
        // or a hot-store seed - rather than being a key-only record seeded from the warm
        // tier. copyUser()/name-rehydration require it; copyPublicKey() does not.
        uint8_t hasFullUser : 1;

        // The node currently exists in NodeDB (hot store or warm tier), per the last
        // maintenance-sweep membership check (may be up to one sweep / 60 s stale). Member
        // entries are the stickiest under LRU eviction, keeping the cache a superset of
        // NodeDB's identities; the bit itself is the keep-alive - nothing expires by TTL.
        uint8_t isMember : 1;
    };
    // The tick stamps, sourceChannel, decodedBitfield, and the 1-bit flags make up the
    // entry's trailing metadata; the flag bits share a single byte, leaving 2 spare bits.
    // Add future booleans as more 1-bit fields here rather than new bytes - the array is
    // 2000 entries in PSRAM, so a fresh byte can cost a whole aligned word. (No exact-size
    // static_assert: sizeof(meshtastic_User) and its trailing padding vary by platform
    // - nanopb packs the generated struct differently on embedded targets - so any fixed byte
    // count is non-portable and would fail the build on some boards.)

    NodeInfoPayloadEntry *nodeInfoPayload = nullptr; // NodeInfo payloads in PSRAM (flat array, linear scan)
    bool nodeInfoPayloadFromPsram = false;           // Tracks allocator for correct deallocation

    // Throttle stamp for the NodeDB fallback direct-response path (non-PSRAM boards).
    // The cache path throttles per target via NodeInfoPayloadEntry::respTick, but
    // the fallback path has no per-node slot to stamp, so it would otherwise emit spoofed
    // replies with no rate limit at all. This single module-global stamp throttles that
    // path to at most one spoofed reply per kNodeInfoResponseThrottleMs across all targets.
    // Coarser than per-target, but the fallback path has nowhere to store per-node state,
    // and a global cap still bounds spoofed transmissions - which is the point of the
    // throttle. 0 = never responded. Guarded by cacheLock. Local uptime (millis).
    uint32_t nodeInfoFallbackLastResponseMs = 0;

    meshtastic_TrafficManagementStats stats;

    // Flag set during alterReceived() when packet should be exhausted.
    // Checked by perhapsRebroadcast() to force hop_limit = 0 only for the
    // matching packet key (from + id). Reset at start of handleReceived().
    bool exhaustRequested = false;
    NodeNum exhaustRequestedFrom = 0;
    PacketId exhaustRequestedId = 0;

    // One-shot guard: warm-start next-hop cache from NodeDB on first maintenance pass.
    bool nextHopPreloaded = false;

    // Anti-entropy cadence for reconcileNodeInfoFromNodeDBLocked(): a full boot seed on the
    // first maintenance pass (once nodeDB is ready), then one reconciliation per hour of
    // sweeps. The write-through hooks give immediacy; this periodic repair self-heals
    // anything they miss (boot ordering, a write path without a hook, future NodeDB paths).
    static constexpr uint8_t kNodeInfoReconcileSweeps = 60; // sweeps between reconciliations (60 x 60 s = 1 h)
    bool nodeInfoSeeded = false;
    uint8_t sweepsSinceNodeInfoReconcile = 0;

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
    NodeInfoPayloadEntry *findNodeInfoEntryMutable(NodeNum node)
    {
        return const_cast<NodeInfoPayloadEntry *>(findNodeInfoEntry(node));
    }
    // spareMembers: when creating would evict an isMember entry, return nullptr instead.
    // The seeding pass uses it so seeding one NodeDB-tier node never churns out another
    // when hot+warm exceeds the cache; the packet path leaves it false (a freshly
    // observed stranger may displace the LRU member - observed freshness has value).
    NodeInfoPayloadEntry *findOrCreateNodeInfoEntry(NodeNum node, bool *usedEmptySlot, bool spareMembers = false);
    uint16_t countNodeInfoEntriesLocked() const;

    // Anti-entropy seeding, under cacheLock: walk the NodeDB hot store (full identity) and
    // warm tier (key-only records) and upsert whatever this cache lacks, marking members
    // and adopting NodeDB's key when a cached key conflicts with the authoritative one.
    // Runs at boot and then hourly (kNodeInfoReconcileSweeps); the write-through hooks give
    // immediacy in between, and per-entry membership refresh happens every sweep. Never
    // sets hasObserved - seeding is knowledge, not observation, so it can never make a
    // silent node servable by the replay path.
    void reconcileNodeInfoFromNodeDBLocked();
    void cacheNodeInfoPacket(const meshtastic_MeshPacket &mp);

    // =========================================================================
    // Traffic Management Logic
    // =========================================================================

    bool shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs);
    bool shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse);

    // Replies go to the requesting packet's unauthenticated `from`, so space them per requester to bound
    // what any one node can be made to receive, plus a global floor on airtime.
    static constexpr uint32_t kDirectResponsePerRequestorMs = 60'000UL;
    static constexpr uint32_t kDirectResponseGlobalMs = 1'000UL;
    static constexpr size_t kDirectResponseTrackedRequestors = 8;
    struct DirectResponseThrottleEntry {
        NodeNum requestor;
        uint32_t lastReplyMs;
    };
    DirectResponseThrottleEntry directResponseSeen[kDirectResponseTrackedRequestors] = {};
    uint32_t lastDirectResponseMs = 0;
    bool directResponseAllowed(NodeNum requestor, uint32_t nowMs);
    bool isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const;
    bool isRateLimited(NodeNum from, uint32_t nowMs);
    bool shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs);

    void logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const;
    void incrementStat(uint32_t *field);
};

static_assert(TRAFFIC_MANAGEMENT_CACHE_SIZE <= UINT16_MAX, "cacheSize() returns uint16_t");

extern TrafficManagementModule *trafficManagementModule;

#endif
