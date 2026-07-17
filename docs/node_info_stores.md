# NodeInfo stores: the base and extended databases

This document is an overview of the node-identity and traffic-state databases that the
TrafficManagementModule (TMM) either owns or leans on. There are four stores in play,
but only three form the identity lookup chain:

1. **NodeDB hot store** - the authoritative `NodeInfoLite` array (identity tier 1).
2. **Warm tier** (`WarmNodeStore`) - minimal persisted records for hot-store evictees
   (identity tier 2).
3. **TMM NodeInfo payload cache** (extended) - the ephemeral **third identity tier**: full
   `User` payloads plus direct-response metadata, in PSRAM.

The fourth store, the **TMM unified cache** (base - flat 10-byte-per-node traffic-shaping
state), is not part of that chain: it sits beside it, keyed by the same NodeNum, and only
its 4-bit cached role acts as a final fallback when all three identity tiers miss.

Sources of truth: `src/mesh/NodeDB.{h,cpp}`, `src/mesh/WarmNodeStore.h`,
`src/modules/TrafficManagementModule.{h,cpp}`, sizing in `src/mesh/mesh-pb-constants.h`.

---

## 1. NodeDB hot store (authoritative)

- **What:** the classic `meshNodes` array of `meshtastic_NodeInfoLite` - full identity as
  flattened fields (names, role, public key, bitfield flags such as `HAS_XEDDSA_SIGNED`;
  position/telemetry live in satellite stores reached via copy-out accessors, not nested
  members). Everything else in this document is a cache or a fallback for it.
- **Capacity:** `MAX_NUM_NODES`, per platform - 250 on native, 120 on nRF52840/generic
  ESP32, 10 on STM32WL (see `mesh-pb-constants.h`).
- **Eviction:** oldest non-protected node when full (`getOrCreateMeshNode`). On eviction
  the node's essentials are **absorbed into the warm tier** (see §2); on re-admission the
  warm record is rehydrated back (`take()`), including the signer bit.
- **Persistence:** the node database file, saved on the usual NodeDB cadence.
- **Authority:** key pinning (`updateUser`'s "Public Key mismatch" drop), signer
  provenance, and identity content all originate here. The lookup helpers that other
  stores mirror:
  - `copyPublicKeyAuthoritative(n, out)` - hot store, then warm tier. The pin reference
    for caches; never consults opportunistic caches.
  - `copyPublicKey(n, out)` - the above, then **TMM's NodeInfo cache as last resort**
    (extends the encrypt-to pool for nodes both tiers have forgotten).
  - `isVerifiedSignerForKey(n, key32)` - key-matched signer verdict across hot + warm.
  - `isKnownXeddsaSigner(n)` - key-agnostic "should this node's signable traffic arrive
    signed", across hot + warm. Gates that check only the hot store would let a
    warm-evicted signer be impersonated with unsigned frames.
  - `getNodeRole(n)` - hot store, then the role cached in the warm tier, else `CLIENT`.

## 2. Warm tier - `WarmNodeStore` (NodeDB-owned)

- **What:** the "long-tail" second tier. When a node ages out of the hot store, a minimal
  record survives so DMs keep encrypting: the key is expensive to re-learn; everything
  else rebuilds from traffic in seconds.
- **Entry:** exactly 40 bytes - `num(4) | last_heard(4) | public_key(32)`. The low 7 bits
  of `last_heard` are stolen for metadata (role: 4 bits, protected category: 2, signer
  bit: 1), leaving ~128 s recency resolution - plenty for LRU ranking.
- **Capacity:** `WARM_NODE_COUNT` (100 on constrained parts; platform-tiered).
- **Eviction:** LRU by `last_heard`, with keyed entries outranking keyless; keyless
  candidates never displace keyed entries.
- **Persistence:** nRF52840 uses a 12 KB raw-flash record-ring below LittleFS
  (append/replay/compact); everywhere else `/prefs/warm.dat`.
- **Membership invariant:** a node lives in the hot **XOR** warm tier. `take()` removes
  the warm record when the node is re-admitted hot, restoring role/protected/signer bits.

## 3. TMM unified cache (base, traffic state)

- **What:** TMM's own flat array of packed 10-byte `UnifiedCacheEntry` records - the
  per-node state behind position dedup, rate limiting, unknown-packet filtering, plus two
  piggybacked caches:
  - `next_hop` - last-byte relay hint, written only from ACK-confirmed NextHopRouter
    decisions (no TTL; keeps the slot alive across sweeps).
  - a **4-bit device role** (split across the top bits of two count bytes) - the _third_
    fallback for role-aware policy after the hot store and warm tier, surviving even
    total NodeDB eviction. Read through `resolveSenderRole()`, refreshed by
    `updateCachedRoleFromNodeInfo()` on observed NodeInfo.
- **Entry layout:** `node(4) | pos_fingerprint(1) | rate_count(1) | unknown_count(1) |
pos_time(1) | rate_unknown_time(1) | next_hop(1)` = 10 bytes, all platforms.
  Timestamps are free-running modular ticks (uint8 / nibbles) with presence carried by
  non-zero sentinels - no epochs, no absolute time.
- **Capacity:** `TRAFFIC_MANAGEMENT_CACHE_SIZE`, per memory class: 2048 (PSRAM S3 /
  native), 500 (medium), 400 (small), 250 (nRF52840 - deliberately class-deviant for
  heap headroom), 0 when `HAS_TRAFFIC_MANAGEMENT=0`. Variant-overridable.
- **Eviction:** linear scan; insertion on a full cache evicts the stalest entry,
  preferring entries without a `next_hop` hint.
- **Persistence:** none - RAM/PSRAM only, rebuilt from traffic.

## 4. TMM NodeInfo payload cache (extended, the ephemeral third tier)

- **What:** a flat PSRAM array of `NodeInfoPayloadEntry` - the full cached `User`
  payload (names, role, key) plus the metadata needed to serve **spoofed direct
  NodeInfo replies** on a target's behalf, independent of NodeDB. Also the last-resort
  key source for `NodeDB::copyPublicKey()`.
- **Availability:** `TMM_HAS_NODEINFO_CACHE` - ESP32 with PSRAM (production home;
  2000 entries is too large for MCU internal RAM), plus native unit-test builds on the
  plain heap so the trust/retention paths run in CI.
- **Entry:** `node`, `user` (full nanopb `User`), tick stamps (`obsTick` 3 min/tick,
  `respTick` 5 s/tick), `sourceChannel`, `decodedBitfield`, and packed 1-bit flags:
  `hasDecodedBitfield`, `keySignerProven`, `hasObserved`, `hasResponded`, `hasFullUser`,
  `isMember`.
- **Capacity:** `kNodeInfoCacheEntries = 2000`, linear scan (NodeInfo traffic is
  low-rate).
- **Persistence:** none - this tier is deliberately ephemeral; it reconstructs from
  NodeDB seeding plus observed traffic after every boot.

### Trust & provenance model

- **Key pin, three layers deep:** an incoming NodeInfo key is checked against
  `copyPublicKeyAuthoritative()` (hot then warm - the same coverage as
  `updateUser`'s own pin), and, failing NodeDB knowledge, against the cache's **own
  previously cached key** (TOFU pin). Mismatches are dropped, never overwritten. A frame
  advertising _our own_ key is dropped outright (impersonation).
- **`keySignerProven`:** set when a frame's XEdDSA signature was router-verified
  (`mp.xeddsa_signed`) or when NodeDB already knew the node as a signer **for the same
  key** (`isVerifiedSignerForKey`). Monotonic per slot; a changed key resets it.
- **Unsigned-identity gate:** a NodeInfo arriving _unsigned_ from a node we have ever
  verified as a signer - per `NodeDB::isKnownXeddsaSigner()`, which covers hot **and
  warm** tiers - drives no cache, role, or `updateUser()` write. (Warm coverage matters:
  a signer evicted to the warm tier would otherwise be forgeable with its own public key
  until re-heard. The same rule guards `Router::checkXeddsaReceivePolicy`'s
  unsigned-broadcast drop.)
- **Serve gate honesty:** only a genuinely _heard_ NODEINFO frame stamps
  `obsTick`/`hasObserved`. Seeding and write-through are knowledge, not observation -
  they can never make a silent node look alive to the replay path. The serve window
  (6 h) and per-target throttle (30 s) are enforced by the sweep-cleared flag bits.

### Consistency with NodeDB (anti-entropy)

Three mechanisms keep this tier a superset of NodeDB's identities:

| Mechanism                                                             | When                                | What                                                                                                                                                                                                                                                          |
| --------------------------------------------------------------------- | ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Write-through hooks (`onNodeIdentityCommitted`, `onNodeKeyCommitted`) | on every NodeDB identity/key commit | immediate upsert; merges rather than overwrites - a keyless commit never costs the cache a learned TOFU key                                                                                                                                                   |
| Reconcile sweep (`reconcileNodeInfoFromNodeDBLocked`)                 | boot seed, then hourly              | walks hot store (full identity) + warm tier (key-only records); adopts NodeDB content under the same keyless-merge rule; transfers signer verdicts only key-matched                                                                                           |
| Membership refresh                                                    | inside the hourly reconcile         | clear-all then re-mark from both tiers (a per-entry NodeDB lookup every sweep would be O(entries x members) under the lock); additions stay immediate via the hooks, explicit removals via `purgeNode()`; a **passive** NodeDB eviction may lag up to an hour |
| Purge hooks (`purgeNode`, `purgeAll`)                                 | NodeDB node removal / reset         | clear both the unified cache and the NodeInfo cache, each under its own compile guard                                                                                                                                                                         |

**Retention:** no timed eviction. Slots die only by LRU displacement on insert, ranked by
trust tiers - members and signer-proven keys are stickiest; the seeding pass additionally
refuses to churn one member out for another (`spareMembers`).

**Key-commit funnel:** every path that writes a remote key into the hot store must route the
write-through. Full-identity commits funnel through `NodeDB::updateUser()`; bare-key commits
(admin-channel learn in `Router::perhapsDecode`, manual verification in
`KeyVerificationModule`) funnel through `NodeDB::commitRemoteKey()`, which carries an explicit
`KeyCommitTrust` provenance (`ManuallyVerified` maps to `proven=true` in this cache). Never
assign `info->public_key` directly - the cache would silently diverge until the next reconcile.

**Enable gate:** the write-through hooks, like the sweep and the packet path, no-op while
`moduleConfig.has_traffic_management` is off, so cache content and cache maintenance are keyed
to the same condition. Corollary: the pubkey-pool superset property only holds while the
module is enabled.

### Tick clocks and wrap safety

All TMM timestamps are free-running modular ticks (uint8 or nibble) derived from `clockMs()`;
modular subtraction gives a correct age only while the true age stays below the counter
period. What keeps each clock honest differs, and matters when touching the sweep:

| Clock                        | Period   | Window/TTL      | Wrap safety                                                                                                                                   |
| ---------------------------- | -------- | --------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| pos (uint8, 6 min/tick)      | 25.6 h   | <=255 ticks     | 60 s sweep clears expired state; margin as low as 1 tick (6 min) at the clamp                                                                 |
| rate (nibble, 5 min/tick)    | 80 min   | <=15 ticks      | sweep, **plus** read-time window reset in `isRateLimited()`                                                                                   |
| unknown (nibble, 1 min/tick) | 16 min   | 12 ticks        | sweep, plus read-time window reset                                                                                                            |
| NodeInfo `obsTick` (3 min)   | 12.8 h   | 120 ticks (6 h) | **sweep only** - `maintainNodeInfoCacheLocked()` clearing `hasObserved` is the sole guarantee the 6 h serve gate never reads an aliased stamp |
| NodeInfo `respTick` (5 s)    | 21.3 min | 6 ticks (30 s)  | sweep only (worst case of a missed clear: one spurious 30 s throttle)                                                                         |

The warm tier is different by design: `WarmNodeStore.last_heard` is an **absolute**
unix-seconds timestamp (quantised to 128 s by the metadata steal), so it cannot wrap until
2106 and needs no sweep - affordable at 100 x 40 B, where the TMM caches chose 1-byte ticks
to stay at 10 B/entry x up to 2048.

Because the NodeInfo clocks are sweep-dependent, the maintenance invariant is compile-time:
`maintainNodeInfoCacheLocked()` is guarded by `TMM_HAS_NODEINFO_CACHE` **alone** (never by
`TRAFFIC_MANAGEMENT_CACHE_SIZE`, which a variant may zero independently), mirroring
`purgeAll()` - a build that has the cache always has its sweep.

### Direct-response behavior under throttle

The per-target response throttle bounds only **our spoofed TX**. A request that arrives inside
the throttle window is **not consumed**: `shouldRespondToNodeInfo()` returns false and the
request forwards through normal relay handling toward the genuine target, which can answer
itself. `nodeinfo_cache_hits` counts only replies actually sent.

---

## How a lookup falls through the tiers

```text
identity/role/key consumer
        │
        ▼
 1. hot store (NodeInfoLite)         full identity, authoritative
        │ miss
        ▼
 2. warm tier (WarmNodeStore)        key + role/protected/signer bits, persisted
        │ miss
        ▼
 3. TMM NodeInfo cache (PSRAM)       full User payloads + TOFU/proven keys, ephemeral
        │ miss                        (role-only: 4-bit role in the unified cache)
        ▼
      defaults (no key; role = CLIENT)
```

The unified cache (§3) sits beside this chain rather than in it: it is traffic-shaping
state keyed by the same NodeNum, whose role bits act as the final role fallback when all
three identity tiers miss.
