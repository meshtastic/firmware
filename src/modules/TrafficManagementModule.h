#pragma once

#include "MeshModule.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#include <algorithm>

#if HAS_TRAFFIC_MANAGEMENT

// Replay provenance gate: when 1 (default), direct responses are spoofed only for nodes whose
// cached key is key-proven (XEdDSA-signed or manually verified), not for trust-on-first-use identities.
// Define as 0 to also serve fresh TOFU-only nodes; bypassed entirely when PKI is excluded.
#ifndef TMM_NODEINFO_REPLAY_REQUIRE_SIGNED
#define TMM_NODEINFO_REPLAY_REQUIRE_SIGNED 1
#endif

// Effective gate: only meaningful when PKI is compiled in.
#if TMM_NODEINFO_REPLAY_REQUIRE_SIGNED && !(MESHTASTIC_EXCLUDE_PKI)
#define TMM_NODEINFO_REPLAY_SIGNED_GATE 1
#else
#define TMM_NODEINFO_REPLAY_SIGNED_GATE 0
#endif

// NodeInfo cache availability. Production home is ESP32+PSRAM (the 2000-entry array is too big
// for MCU internal RAM); native unit-test builds enable it on the plain heap so the cache paths
// run in CI (tests needing the NodeDB fallback call dropNodeInfoCacheForTest()).
#if (defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)) || (defined(ARCH_PORTDUINO) && defined(PIO_UNIT_TESTING))
#define TMM_HAS_NODEINFO_CACHE 1
#else
#define TMM_HAS_NODEINFO_CACHE 0
#endif

/// Packet inspection and traffic shaping: position dedup, per-node rate limiting, unknown-packet
/// filtering, NodeInfo direct response, and the next-hop/role overflow caches. One flat 10-byte
/// unified cache backs all per-node features; see https://meshtastic.org/docs/development/reference/node-info-stores for the
/// store overview.
class TrafficManagementModule : public MeshModule, private concurrency::OSThread
{
  public:
    // Top-senders count carried in DeviceMetrics.top_senders (gossiped top-sender rate budget). Declared in
    // the public section so it is visible to snapshotTopSenders() below
    // (a later member is not in scope in a parameter list).
    static constexpr uint16_t kTopSendersCount = 3;

    TrafficManagementModule();
    ~TrafficManagementModule();

    // Singleton - no copying or moving
    TrafficManagementModule(const TrafficManagementModule &) = delete;
    TrafficManagementModule &operator=(const TrafficManagementModule &) = delete;

    /// Snapshot of the module's counters (thread-safe).
    meshtastic_TrafficManagementStats getStats() const;
    /// Zero all counters (thread-safe).
    void resetStats();
    /// Placeholder for the removed router_preserve_hops stat.
    void recordRouterHopPreserved();

    /// Store a confirmed last-byte next hop for `dest`. Called only from NextHopRouter's
    /// ACK-confirmed decision - the byte must come from a bidirectionally-verified relay.
    void setNextHop(NodeNum dest, uint8_t nextHopByte);
    /// Cached next-hop byte for `dest`, 0 if unknown.
    uint8_t getNextHopHint(NodeNum dest);
    /// Forget the cached next hop for `dest` (how NextHopRouter decays a failing route).
    void clearNextHop(NodeNum dest);

    /// Warm-start the next-hop cache from persisted NodeInfoLite hints so confirmed hops survive
    /// hot-store eviction. @return true if it ran; false if prerequisites (cache, nodeDB) weren't
    /// ready and the caller should retry on a later pass.
    bool preloadNextHopsFromNodeDB();

    /// Last-resort key source for NodeDB::copyPublicKey() after the hot and warm tiers miss.
    /// Copies the 32-byte key for `node` into out[32]; `keyProven` (optional) reports whether
    /// the key is proven (XEdDSA-signed or manually verified) vs trust-on-first-use. Thread-safe.
    bool copyPublicKey(NodeNum node, uint8_t out[32], bool *keyProven = nullptr) const;

    /// Copy the full cached User for `node` (used by NodeDB to rehydrate a re-admitted node's
    /// name - the warm tier keeps keys but not names). False on miss or key-only records.
    /// `keyProven` (optional) reports the cached key's provenance. Thread-safe.
    bool copyUser(NodeNum node, meshtastic_User &out, bool *keyProven = nullptr) const;

    /// Write-through hook from NodeDB::updateUser(): upsert the committed identity immediately
    /// (the reconcile sweep remains the backstop). NodeDB's key is authoritative, but a keyless
    /// commit keeps a TOFU key this cache already holds; never touches the observation stamp.
    /// No-op while the module is disabled in moduleConfig (maintenance is gated the same way).
    void onNodeIdentityCommitted(NodeNum node, const meshtastic_User &user, bool signerKnown);

    /// Key-only commit hook for key writes that bypass updateUser (admin-key learn, manual key
    /// verification). A changed key resets provenance; pass proven=true only when the commit
    /// itself established possession. Never touches the observation stamp. Thread-safe.
    /// No-op while the module is disabled in moduleConfig (maintenance is gated the same way).
    void onNodeKeyCommitted(NodeNum node, const uint8_t key32[32], bool proven);

    /// Zero one node's slots in both caches (identity, key, provenance, role, next-hop, dedup
    /// state). Called by NodeDB removal so no TMM tier resurrects a deliberately deleted node;
    /// passive eviction is unaffected. Thread-safe.
    void purgeNode(NodeNum node);
    /// Clear both cache tables outright (resetNodes / factory reset). Thread-safe.
    void purgeAll();

    /// True when perhapsRebroadcast() must force hop_limit=0 for this packet, regardless of
    /// router_preserve_hops or favorite-node logic (set by alterReceived()).
    bool shouldExhaustHops(const meshtastic_MeshPacket &mp) const
    {
        return exhaustRequested && exhaustRequestedFrom == getFrom(&mp) && exhaustRequestedId == mp.id;
    }

    // =========================================================================
    // Antispam: probation greylist, optional relay budget, KNOWN_SINCE / NO_RELAY
    // =========================================================================

    /// True when this packet should be rebroadcast. False means deliver locally
    /// and skip TX. want_ack / ROUTING_APP / ADMIN_APP always return true.
    bool shouldRelay(const meshtastic_MeshPacket &mp) const;

    /// hop_limit for the relayed copy: min(original, probation cap, congestion cap).
    uint8_t relayHopCap(const meshtastic_MeshPacket &mp) const;

    /// Charge one rebroadcast to the sender's relay budget; gossips NO_RELAY on first exhaustion.
    void recordRelayed(const meshtastic_MeshPacket &mp);

    /// Fill `out` with the top senders this rate window (node=0 if unused).
    void snapshotTopSenders(meshtastic_TopSender (&out)[kTopSendersCount]) const;

    /// Ingest a neighbor's top-senders samples. No-op when budget gossip is off.
    void ingestNeighborTopSenders(NodeNum neighbor, const meshtastic_TopSender *entries, pb_size_t count);

    // Test hooks (antispam state introspection).
    int peekProbationStateForTest(NodeNum node);                                // -1 untracked, 0 established, 1 in-probation
    uint32_t peekRelayedCountForTest(NodeNum node);                             // windowed relayed-for count
    bool peekNoRelayForTest(NodeNum node);                                      // gossiped/local NO_RELAY in force
    int peekSenderBudgetForTest(NodeNum sender, uint32_t *medianOut = nullptr); // -1 untracked
    uint8_t peekVouchCountForTest(NodeNum attester, NodeNum subject);
    uint8_t peekVouchSubjectsForTest(NodeNum attester);
    uint8_t peekAttestQuorumForTest(NodeNum subject);
    int16_t peekRssiChannelForTest(NodeNum node);
    /// Non-zero when a promotion lease is armed; 0 when permanent or not promoted.
    uint8_t peekPromotedWindowTickForTest(NodeNum node);
    bool peekPromotedForTest(NodeNum node);
    /// 0 = anonymous, 1 = signed observation, 2 = neighbor-attested, 3 = manual.
    uint8_t trustLevelForTest(NodeNum node);
    void setLastSignedSecsForTest(NodeNum node, uint32_t secs);
    bool peekNoRelayLocalForTest(NodeNum node);
    /// 0xFFFFFFFF = production (Time::getUptimeSecs() or test clock); otherwise the stored value.
    inline static uint32_t s_testUptimeSecs = 0xFFFFFFFFu;
    static void setUptimeSecsForTest(uint32_t secs) { s_testUptimeSecs = secs; }
    inline static int s_testCongestionPct = -1;

    inline static uint32_t s_testNowMs = 0;
#ifdef PIO_UNIT_TESTING
    static uint32_t clockMs() { return s_testNowMs; }
#else
    static uint32_t clockMs();
#endif

  protected:
    /// Inspect a received packet; may consume it (STOP) for dedup/rate/unknown/direct-response.
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    /// Promiscuous: this module inspects every packet.
    bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    /// Mutate relayed packets in place (position precision clamp).
    void alterReceived(meshtastic_MeshPacket &mp) override;
    /// 60 s maintenance sweep: expire timed state, saturate tick stamps, reconcile with NodeDB.
    int32_t runOnce() override;
    /// Clear all per-node traffic state (protected for test shims).
    void flushCache();
    /// Test introspection: the cached role for `node`, or -1 when it has no entry
    /// (distinguishes "not tracked" from CLIENT == 0).
    int peekCachedRole(NodeNum node);

    /// Test hook: force a cached NodeInfo entry's key to XEdDSA-signed so replay-gate tests
    /// can skip a full XEdDSA verification. No-op if absent.
    void markKeyXeddsaSignedForTest(NodeNum node);

    /// Test hook: free the NodeInfo cache so the NodeDB fallback path can be exercised in
    /// builds where the cache is compiled in. No-op when already absent.
    void dropNodeInfoCacheForTest();

    /// Test introspection: NodeInfo flag bits for `node` (-1 if absent): bit0 hasObserved,
    /// bit1 isMember, bit2 hasFullUser, bit3 keyProven (keyXeddsaSigned | keyManuallyVerified).
    int peekNodeInfoFlagsForTest(NodeNum node);

    /// Test introspection: NodeInfo cache capacity (kNodeInfoCacheEntries), so tests can
    /// fill the cache exactly and force the tiered-LRU eviction paths.
    static constexpr uint16_t nodeInfoCacheCapacityForTest() { return kNodeInfoCacheEntries; }

  private:
    // 10-byte packed entry, all platforms. Tick stamps are free-running modular counters with
    // non-zero presence sentinels; the 4-bit cached role rides the top bits of the two count
    // bytes (tier-3 role fallback). Full layout and rationale:
    // https://meshtastic.org/docs/development/reference/node-info-stores.
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

        /// Packets seen in the current rate window (low 6 bits).
        uint8_t getRateCount() const { return rate_count & 0x3F; }
        /// Set the rate-window count, preserving the role bits.
        void setRateCount(uint8_t c) { rate_count = static_cast<uint8_t>((rate_count & 0xC0) | (c & 0x3F)); }
        /// Unknown packets seen in the current window (low 6 bits).
        uint8_t getUnknownCount() const { return unknown_count & 0x3F; }
        /// Set the unknown-window count, preserving the role bits.
        void setUnknownCount(uint8_t c) { unknown_count = static_cast<uint8_t>((unknown_count & 0xC0) | (c & 0x3F)); }
        /// Cached 4-bit device role, reassembled from the two count bytes' top bits.
        uint8_t getCachedRole() const { return static_cast<uint8_t>(((rate_count >> 6) << 2) | (unknown_count >> 6)); }
        /// Store a 4-bit device role across the two count bytes' top bits.
        void setCachedRole(uint8_t role)
        {
            rate_count = static_cast<uint8_t>((rate_count & 0x3F) | ((role >> 2) << 6));
            unknown_count = static_cast<uint8_t>((unknown_count & 0x3F) | ((role & 0x03) << 6));
        }
        /// Rate-window tick nibble.
        uint8_t getRateTime() const { return (rate_unknown_time >> 4) & 0x0F; }
        /// Unknown-window tick nibble.
        uint8_t getUnknownTime() const { return rate_unknown_time & 0x0F; }
        /// Set the rate-window tick nibble.
        void setRateTime(uint8_t t) { rate_unknown_time = static_cast<uint8_t>((rate_unknown_time & 0x0F) | ((t & 0x0F) << 4)); }
        /// Set the unknown-window tick nibble.
        void setUnknownTime(uint8_t t) { rate_unknown_time = static_cast<uint8_t>((rate_unknown_time & 0xF0) | (t & 0x0F)); }
    };
    static_assert(sizeof(UnifiedCacheEntry) == 10, "UnifiedCacheEntry should be 10 bytes");

    /// Unified cache capacity. Plain array, linear scan (same idiom as WarmNodeStore); insertion
    /// on a full cache evicts the stalest entry, preferring ones without a next_hop hint.
    static constexpr uint16_t cacheSize() { return TRAFFIC_MANAGEMENT_CACHE_SIZE; }

    // NodeInfo cache (PSRAM-backed on hardware, heap in native tests): flat payload array,
    // linear scan, trust/membership-tiered LRU eviction on insert. NodeInfo traffic is
    // low-rate, so full scans are fine.
    static constexpr uint16_t kNodeInfoCacheEntries = 2000;
    /// NodeInfo cache capacity.
    static constexpr uint16_t nodeInfoTargetEntries() { return kNodeInfoCacheEntries; }

    // Free-running modular tick clocks derived from clockMs(); modular subtraction gives correct
    // age while true age stays below the counter period. Presence is carried by non-zero
    // sentinels (unified cache) or explicit validity bits (NodeInfo cache).
    static constexpr uint32_t kPosTimeTickMs = 360'000UL;    // 6 min/tick (uint8: 25.6 h period)
    static constexpr uint32_t kRateTimeTickMs = 300'000UL;   // 5 min/tick (nibble: 80 min period)
    static constexpr uint32_t kUnknownTimeTickMs = 60'000UL; // 1 min/tick (nibble: 16 min period)

    /// Current position-clock tick (6 min/tick).
    static uint8_t currentPosTick() { return static_cast<uint8_t>(clockMs() / kPosTimeTickMs); }
    /// Current rate-clock tick nibble (5 min/tick).
    static uint8_t currentRateTick() { return static_cast<uint8_t>((clockMs() / kRateTimeTickMs) & 0x0F); }
    /// Current unknown-clock tick nibble (1 min/tick).
    static uint8_t currentUnknownTick() { return static_cast<uint8_t>((clockMs() / kUnknownTimeTickMs) & 0x0F); }

    // NodeInfo observation tick (same idiom). The 60 s sweep clears the presence bit once the serve
    // window passes, so the stamp is never read near its uint8 aliasing horizon. (Response throttling
    // no longer lives here - it is the fixed per-requester/per-target RAM tables below, which use
    // wrap-safe uint32 ms compares and so need no tick clock or sweep.)
    static constexpr uint32_t kNodeInfoObsTickMs = 180000UL;  // 3 min/tick (12.8 h period)
    static constexpr uint8_t kNodeInfoMaxServeAgeTicks = 120; // 6 h serve window

    /// Current NodeInfo observation tick (3 min/tick).
    static uint8_t currentObsTick() { return static_cast<uint8_t>(clockMs() / kNodeInfoObsTickMs); }
    static_assert(kNodeInfoMaxServeAgeTicks * kNodeInfoObsTickMs == 6UL * 60UL * 60UL * 1000UL,
                  "cache serve window must equal the fallback path's 6 h");

    /// 8-bit position fingerprint from truncated lat/lon: low 4 significant bits of each, so
    /// adjacent grid cells never collide (collisions need 16+ cells apart in both dimensions).
    static uint8_t computePositionFingerprint(int32_t lat_truncated, int32_t lon_truncated, uint8_t precision);

    // =========================================================================
    // Cache Storage
    // =========================================================================

    mutable concurrency::Lock cacheLock; // Protects all cache access
    UnifiedCacheEntry *cache = nullptr;  // Flat unified cache (linear scan; all platforms)
    bool cacheFromPsram = false;         // Tracks allocator for correct deallocation

    struct NodeInfoPayloadEntry {
        // Node identifier for this slot; 0 means unused.
        NodeNum node;

        // Cached NODEINFO_APP payload, independent of NodeDB; serves the PSRAM-backed
        // direct-response path and the last-resort pubkey pool.
        meshtastic_User user;

        // Tick of the last genuinely HEARD NODEINFO frame (kNodeInfoObsTickMs clock). Drives the
        // replay staleness gate and LRU age; seeding/write-through never touch it, so a spoofed
        // reply is only ever backed by genuine recent observation. Validity: hasObserved.
        uint8_t obsTick;

        // Channel where we most recently heard this node's NodeInfo.
        uint8_t sourceChannel;

        // Cached decoded bitfield from the source packet (non-OK_TO_MQTT bits are preserved
        // in direct replies). Validity: hasDecodedBitfield.
        uint8_t decodedBitfield;

        // 1-bit flags, packed into one byte (2 spare bits; add future booleans here rather
        // than new bytes - the array is 2000 entries). Per-node observed RSSI class + channel
        // live in the antispam table (AntispamEntry.rssiClass/channel, refreshed on every
        // observation) - this entry's sourceChannel is the last-heard NodeInfo channel only.

        // The source packet carried a decoded bitfield (so decodedBitfield is meaningful).
        uint8_t hasDecodedBitfield : 1;

        // Key provenance, split by how possession was established (either one implies "proven" -
        // read the pair via keyProven()). Both are monotonic per slot until the key rotates (the
        // key-pin checks forbid the key changing underneath them), and TOFU keys start at 0.
        //
        // keyXeddsaSigned: an XEdDSA signature was verified for user.public_key - a heard signed
        // frame, or inherited from NodeDB via isVerifiedSignerForKey.
        uint8_t keyXeddsaSigned : 1;
        // keyManuallyVerified: the user confirmed possession of exactly this key out-of-band
        // (QR / fingerprint). Routed here via onNodeKeyCommitted(proven) and re-seeded from the
        // hot-store is_key_manually_verified bit at reconcile (warm records don't carry it).
        uint8_t keyManuallyVerified : 1;

        // obsTick is valid: a NODEINFO frame was actually heard within the observation clock's
        // horizon. Cleared by the sweep once the serve window passes (saturation).
        uint8_t hasObserved : 1;

        // `user` carries a real User payload (from an observed frame or hot-store seed) rather
        // than a key-only warm-tier record. copyUser()/name-rehydration require it.
        uint8_t hasFullUser : 1;

        // Node currently exists in NodeDB (hot or warm), per the last hourly reconcile pass
        // (write-through hooks set it immediately on commit; purgeNode clears immediately on
        // removal; a passive NodeDB eviction may lag up to an hour). Member entries are
        // stickiest under LRU; the bit is the keep-alive (no TTL).
        uint8_t isMember : 1;

        // Possession proven by either channel - the "key-proven" predicate the replay gate,
        // eviction tiering, and NodeDB pubkey-pool callers consume.
        bool keyProven() const { return keyXeddsaSigned || keyManuallyVerified; }
    };
    // No exact-size static_assert: sizeof(meshtastic_User) and its padding vary by platform, so
    // any fixed byte count would fail the build on some boards.

    NodeInfoPayloadEntry *nodeInfoPayload = nullptr; // NodeInfo payloads (flat array; PSRAM on hardware, heap in tests)
    bool nodeInfoPayloadFromPsram = false;           // Tracks allocator for correct deallocation

    mutable meshtastic_TrafficManagementStats
        stats; // mutable: updated from const methods (stats counters don't affect observable const state)

    // Set during alterReceived() when the packet's hops should be exhausted; checked by
    // perhapsRebroadcast() for the matching packet key. Reset at start of handleReceived().
    bool exhaustRequested = false;
    NodeNum exhaustRequestedFrom = 0;
    PacketId exhaustRequestedId = 0;

    // One-shot guard: warm-start next-hop cache from NodeDB on first maintenance pass.
    bool nextHopPreloaded = false;

    // Reconcile cadence: full boot seed on the first maintenance pass, then hourly. The
    // write-through hooks give immediacy; this periodic repair self-heals anything they miss.
    static constexpr uint8_t kNodeInfoReconcileSweeps = 60; // sweeps between reconciliations (60 x 60 s = 1 h)
    bool nodeInfoSeeded = false;
    uint8_t sweepsSinceNodeInfoReconcile = 0;

    // Per-node greylist / relay-budget / trust state. Separate from the 10-byte
    // unified cache: windowed counters and first-seen uptime need real fields.
    struct __attribute__((packed)) AntispamEntry {
        NodeNum node;
        uint32_t firstSeenSecs;  // uptime seconds; valid when hasFirstSeen
        uint32_t lastSignedSecs; // uptime of last verified signature; valid when hasLastSigned
        uint32_t promotedAtSecs; // uptime when the promotion lease was armed; 0 = permanent
        NodeNum noRelayClaimer;  // last gossip attester; 0 = local exhaustion
        uint32_t noRelayClaimMs;
        NodeNum budgetSampleMark[3];
        uint8_t relayedCount;
        uint8_t windowTick; // 5-min nibble clock; valid when hasWindow
        uint8_t promoted : 1;
        uint8_t noRelay : 1;
        uint8_t noRelayLocal : 1;
        uint8_t trustLevel : 2; // 0 anonymous, 1 signed, 2 neighbor-attested, 3 manual
        uint8_t hasFirstSeen : 1;
        uint8_t hasLastSigned : 1;
        uint8_t hasWindow : 1;
        uint8_t budgetSampleCount;
        uint8_t budgetSamples[3];
        uint8_t rssiClass;
        uint8_t channel;
    };
    static_assert(sizeof(AntispamEntry) == 45, "AntispamEntry should be 45 bytes");

    static constexpr uint16_t antispamCacheSize()
    {
        return TRAFFIC_MANAGEMENT_CACHE_SIZE > 0 ? std::min<uint16_t>(TRAFFIC_MANAGEMENT_CACHE_SIZE, ANTISPAM_CACHE_SIZE) : 0;
    }

    mutable AntispamEntry *antispam =
        nullptr; // mutable: const query paths (inProbation, effectiveRateThresholdLocked, ...) only read or slot-fill it
    bool antispamFromPsram = false;

    bool inProbation(NodeNum node) const;
    bool inProbationLocked(const AntispamEntry *entry) const;
    uint32_t observedAgeSecsLocked(const AntispamEntry *entry) const;
    bool attesterObservedEnoughLocked(const AntispamEntry *attesterEntry, uint32_t minSecs) const;
    static bool relayBudgetExempt(const meshtastic_MeshPacket &mp);
    bool noteFirstSeen(NodeNum node, uint8_t channel, uint8_t rssiClass, bool signedObserved);
    bool isEstablishedForVouching(NodeNum node) const;
    uint32_t effectiveRateThreshold(NodeNum sender) const;
    uint32_t effectiveRateThresholdLocked(NodeNum sender) const;
    float currentCongestionPct() const;
    bool sendNoRelayGossip(NodeNum subject);
    bool sendKnownSinceGossip(NodeNum subject);
    bool handleIdAttestation(const meshtastic_MeshPacket &mp);
    void stampVouchObservationLocked(NodeNum attester, NodeNum subject);
    bool vouchWithinCapsLocked(NodeNum attester, NodeNum subject) const;
    void stampAttestQuorumLocked(NodeNum attester, NodeNum subject);
    uint8_t attestQuorumCountLocked(NodeNum subject) const;
    uint32_t attestationMinDistinctAttestersLocked() const;
    uint32_t l2FloorSecs() const;
    bool l2VouchEligibleLocked(const AntispamEntry *subject, NodeNum attester, bool signedObserved) const;
    void stampNoRelayClaimLocked(NodeNum attester, NodeNum subject, uint32_t nowMs);
    uint8_t noRelayClaimerCountLocked(NodeNum subject, uint32_t nowMs) const;
    bool observeGroupCooccurrence(NodeNum node, uint8_t channel, uint8_t rssiClass);
    bool isInFlaggedGroup(NodeNum node, uint8_t channel, uint8_t rssiClass) const;
    bool isInFlaggedGroupLocked(uint8_t channel, uint8_t rssiClass) const;
    uint32_t groupBudgetLocked(uint8_t channel, uint8_t rssiClass) const;
    uint32_t groupBudgetForTest(uint8_t channel, uint8_t rssiClass);
    uint32_t uptimeSecs() const;
    uint8_t rssiClassOf(const meshtastic_MeshPacket &mp);
    AntispamEntry *findAntispamEntry(NodeNum node) const;
    /// Find or create the antispam entry for `node` (oldest-first eviction
    /// when full). nullptr when the table is compiled out or full with no
    /// eviction target. Caller must hold cacheLock.
    AntispamEntry *findOrCreateAntispamEntry(NodeNum node, bool *isNew);
    void clearAntispamAuxLocked(NodeNum node);
    /// Allocate the antispam table alongside the unified cache.
    /// Called from the constructor (single-threaded); no-op when the unified
    /// cache is compiled out.
    void initAntispamCache();
    /// Per-sweep maintenance of the antispam table (window rollover,
    /// probation expiry, group-cell decay). Caller must hold cacheLock.
    void maintainAntispamLocked();

    // Group co-occurrence table (channel x rssiClass x 5-min window).
    static constexpr uint16_t kGroupObsEntries = 16;
    struct GroupObsCell {
        uint8_t channel;
        uint8_t rssiClass;
        uint8_t windowTick;
        uint8_t freshCount;
        uint8_t inUse;
        uint8_t flagged;
    };
    GroupObsCell groupObs[kGroupObsEntries] = {};
    uint32_t groupMedian[kGroupObsEntries] = {};

    uint32_t lastVouchSentMs = 0;

    static constexpr uint16_t kVouchObsEntries = 16;
    struct VouchObsCell {
        NodeNum attester;
        NodeNum subject;
        uint8_t count;
        uint8_t windowTick;
    };
    VouchObsCell vouchObs[kVouchObsEntries] = {};

    static constexpr uint16_t kAttestQuorumEntries = 16;
    struct AttestQuorumCell {
        NodeNum subject;
        NodeNum attester;
        uint8_t windowTick;
    };
    AttestQuorumCell attestQuorum[kAttestQuorumEntries] = {};

    static constexpr uint16_t kNoRelayClaimEntries = 16;
    struct NoRelayClaimCell {
        NodeNum attester;
        NodeNum subject;
        uint8_t windowTick;
        uint32_t claimMs;
    };
    NoRelayClaimCell noRelayClaims[kNoRelayClaimEntries] = {};

    // =========================================================================
    // Cache Operations
    // =========================================================================

    /// Find or create the unified-cache entry for `node` (stalest-first eviction when full).
    UnifiedCacheEntry *findOrCreateEntry(NodeNum node, bool *isNew);

    /// Find an existing unified-cache entry (no creation).
    UnifiedCacheEntry *findEntry(NodeNum node);

    /// Resolve a sender's device role for the position hot path. The tier-3 cache is
    /// authoritative once seeded (NodeDB is scanned only on first tracking), so the read is O(1)
    /// and survives the node aging out of both NodeDB stores. Caller must hold cacheLock.
    meshtastic_Config_DeviceConfig_Role resolveSenderRole(NodeNum from, UnifiedCacheEntry *entry, bool isNew);

    /// Refresh the tier-3 role cache from an observed NodeInfo (the same event that updates
    /// NodeDB's role). Updates only nodes already tracked. Takes cacheLock.
    void updateCachedRoleFromNodeInfo(const meshtastic_MeshPacket &mp);

    /// Find an existing NodeInfo cache entry (no creation).
    const NodeInfoPayloadEntry *findNodeInfoEntry(NodeNum node) const;
    /// Mutable variant of findNodeInfoEntry().
    NodeInfoPayloadEntry *findNodeInfoEntryMutable(NodeNum node)
    {
        return const_cast<NodeInfoPayloadEntry *>(findNodeInfoEntry(node));
    }
    /// Find or create a NodeInfo cache entry, evicting by trust/membership tier when full.
    /// With spareMembers, returns nullptr instead of evicting an isMember entry (the seeding
    /// pass never churns one NodeDB-tier node out for another; the packet path may).
    NodeInfoPayloadEntry *findOrCreateNodeInfoEntry(NodeNum node, bool *usedEmptySlot, bool spareMembers = false);
    /// Number of occupied NodeInfo cache slots. Caller must hold cacheLock.
    uint16_t countNodeInfoEntriesLocked() const;

    /// 60 s NodeInfo-cache maintenance under cacheLock: saturate the expired obsTick stamp (wrap-safety
    /// for the modular clock) and run the boot/hourly reconcile. Guarded by TMM_HAS_NODEINFO_CACHE alone
    /// (never the unified cache size); see https://meshtastic.org/docs/development/reference/node-info-stores "Tick clocks and
    /// wrap safety".
    void maintainNodeInfoCacheLocked();

    /// Anti-entropy under cacheLock: upsert hot-store + warm-tier records this cache lacks (never sets
    /// hasObserved - seeding is knowledge, not observation), and refresh isMember from both NodeDB
    /// tiers. Cost/lag: https://meshtastic.org/docs/development/reference/node-info-stores "Consistency with NodeDB
    /// (anti-entropy)".
    void reconcileNodeInfoFromNodeDBLocked();
    /// Learn an observed NODEINFO frame into the cache (key hygiene + provenance rules apply).
    void cacheNodeInfoPacket(const meshtastic_MeshPacket &mp);

    // =========================================================================
    // Traffic Management Logic
    // =========================================================================

    /// True when this position broadcast duplicates the sender's last one within the dedup window.
    bool shouldDropPosition(const meshtastic_MeshPacket *p, const meshtastic_Position *pos, uint32_t nowMs);
    /// Decide (and with sendResponse, emit) a spoofed direct NodeInfo reply for a unicast request.
    bool shouldRespondToNodeInfo(const meshtastic_MeshPacket *p, bool sendResponse);

    // Direct-response throttles bounding the reflector risk of spoofed replies: three fixed bounds
    // (per requester, per target, 1 s global airtime floor) via 8-slot LRU RAM tables, wrap-safe and
    // PSRAM-agnostic. Design & rationale: https://meshtastic.org/docs/development/reference/traffic-management-internals
    // "Throttling direct responses".
    static constexpr uint32_t kDirectResponsePerRequesterMs = 60'000UL;
    static constexpr uint32_t kDirectResponsePerTargetMs = 60'000UL;
    static constexpr uint32_t kDirectResponseGlobalMs = 1'000UL;
    static constexpr size_t kDirectResponseTrackedNodes = 8;
    struct DirectResponseThrottleEntry {
        NodeNum key;          // requester or target node; 0 = unused slot
        uint32_t lastReplyMs; // clockMs() of our last reply keyed on this node
    };
    DirectResponseThrottleEntry directRequesterSeen[kDirectResponseTrackedNodes] = {};
    DirectResponseThrottleEntry directTargetSeen[kDirectResponseTrackedNodes] = {};
    uint32_t lastDirectResponseMs = 0;
    /// True (and records the send) when a spoofed direct reply to `requester` for `target` is within
    /// every throttle window; false throttles it. Caller must NOT hold cacheLock (this takes it).
    bool directResponseAllowed(NodeNum requester, NodeNum target, uint32_t nowMs);
    /// Slot in `table` to stamp for `key` at `nowMs`, or nullptr if `key` is still within `windowMs`
    /// of its last reply (throttled). Does not stamp - the caller stamps only once all windows pass.
    static DirectResponseThrottleEntry *directResponseSlot(DirectResponseThrottleEntry *table, NodeNum key, uint32_t nowMs,
                                                           uint32_t windowMs);
    /// True when the requestor is within the role-clamped hop limit for direct responses.
    bool isMinHopsFromRequestor(const meshtastic_MeshPacket *p) const;
    /// True when `from` exceeded the configured packet budget for the current rate window.
    bool isRateLimited(NodeNum from, uint32_t nowMs);
    /// Same decision as isRateLimited but for callers that already hold cacheLock
    /// (the binary semaphore is not recursive; a second lock would deadlock).
    bool isRateLimitedLocked(NodeNum from, uint32_t nowMs);
    /// Read-only rate-limit check; never increments the window counter.
    bool peekRateLimitedLocked(NodeNum from, uint32_t nowMs) const;
    /// True when `p`'s sender exceeded the undecodable-packet threshold for the current window.
    bool shouldDropUnknown(const meshtastic_MeshPacket *p, uint32_t nowMs);

    /// Log a traffic action (drop/respond/clamp) with port name and packet routing context.
    void logAction(const char *action, const meshtastic_MeshPacket *p, const char *reason) const;
    /// Increment a stats counter under cacheLock.
    void incrementStat(uint32_t *field);
    /// Increment a stats counter without taking cacheLock (caller holds it).
    void incrementStatLocked(uint32_t *field) const;
};

static_assert(TRAFFIC_MANAGEMENT_CACHE_SIZE <= UINT16_MAX, "cacheSize() returns uint16_t");

extern TrafficManagementModule *trafficManagementModule;

#endif
