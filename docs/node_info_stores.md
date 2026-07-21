# NodeInfo stores: the base and extended databases

This document is an overview of the node-identity and traffic-state databases that the
TrafficManagementModule (TMM) either owns or leans on. There are four stores in play, but
only three form the identity lookup chain:

1. **NodeDB hot store** - the authoritative `NodeInfoLite` array (identity tier 1).
2. **Warm tier** (`WarmNodeStore`) - minimal persisted records for hot-store evictees
   (identity tier 2).
3. **TMM NodeInfo payload cache** (extended) - the ephemeral **third identity tier**: full
   `User` payloads plus direct-response metadata; PSRAM-backed on hardware, plain heap in
   native tests.

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
- **Persistence:** the node database file in LittleFS, saved on the usual NodeDB cadence.
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
  of `last_heard` are omitted, and replaced with metadata (role: 4 bits, protected
  category: 2, signer bit: 1), leaving ~128 s recency resolution - plenty for LRU ranking.
- **Capacity:** `WARM_NODE_COUNT` (100 on constrained parts; platform-tiered).
- **Eviction:** LRU by `last_heard`, with keyed entries outranking keyless; keyless
  candidates never displace keyed entries.
- **Persistence:** nRF52840 uses a 12 KB raw-flash record-ring below LittleFS
  (append/replay/compact); everywhere else `/prefs/warm.dat` (LittleFS).
- **Membership invariant:** a node lives in the hot **XOR** warm tier. `take()` removes
  the warm record when the node is re-admitted hot, restoring role/protected/signer bits.

## 3. TMM unified cache (base, traffic state)

- **What:** TMM's own flat array of packed 10-byte `UnifiedCacheEntry` records - the
  per-node state behind position dedup, rate limiting, unknown-packet filtering, plus two
  piggybacked caches:
  - `next_hop` - last-byte relay hint, written only from ACK-confirmed NextHopRouter
    decisions (no TTL; keeps the slot alive across sweeps).
  - a **4-bit device role** (split across the top bits of two count bytes) - the _third_
    fallback for role-aware policy after the hot store and warm tier, surviving even total
    NodeDB eviction. Read through `resolveSenderRole()`, refreshed by
    `updateCachedRoleFromNodeInfo()` on observed NodeInfo.
- **Entry layout:**
  `node(4) | pos_fingerprint(1) | rate_count(1) | unknown_count(1) | pos_time(1) | rate_unknown_time(1) | next_hop(1)`
  = 10 bytes, all platforms. Timestamps are free-running modular ticks (uint8 / nibbles)
  with presence carried by non-zero sentinels - no epochs, no absolute time.
- **Capacity:** `TRAFFIC_MANAGEMENT_CACHE_SIZE`, per memory class: 2048 (PSRAM S3 /
  native), 500 (medium), 400 (small), 250 (nRF52840 - deliberately class-deviant for heap
  headroom), 0 when `HAS_TRAFFIC_MANAGEMENT=0`. Variant-overridable.
- **Eviction:** linear scan; insertion on a full cache evicts the stalest entry,
  preferring to keep entries with a `next_hop` hint **or** a cached special (non-`CLIENT`)
  role - the long-tail state this cache exists to retain (`findOrCreateEntry`'s `preferred`
  test covers both, not just `next_hop`).
- **Persistence:** none - PSRAM (or heap) only, rebuilt from traffic.

## 4. TMM NodeInfo payload cache (extended, the ephemeral third tier)

- **What:** a flat array of `NodeInfoPayloadEntry` (PSRAM-backed on hardware; see
  Availability) - the full cached `User` payload (names, role, key) plus the metadata that
  backs TMM's **spoofed direct NodeInfo replies** on a target's behalf, independent of
  NodeDB (the serve/throttle behaviour is documented in
  [traffic_management_module.md](traffic_management_module.md)). Also the last-resort key
  source for `NodeDB::copyPublicKey()`.
- **Availability:** `TMM_HAS_NODEINFO_CACHE` - ESP32 with PSRAM (production home; 2000
  entries is too large for MCU internal RAM), plus native unit-test builds on the plain
  heap so the trust/retention paths run in CI.
- **Entry:** `node`, `user` (full nanopb `User`), the `obsTick` recency stamp (3 min/tick),
  `sourceChannel`, `decodedBitfield`, and packed 1-bit flags: `hasDecodedBitfield`,
  `keyXeddsaSigned`, `keyManuallyVerified`, `hasObserved`, `hasFullUser`, `isMember`. (The direct-response throttle
  no longer keeps per-entry state here - it is a pair of separate RAM tables; see the module
  doc.)
- **Capacity:** `kNodeInfoCacheEntries = 2000`, linear scan (NodeInfo traffic is
  low-rate).
- **Persistence:** none - this tier is deliberately ephemeral; it reconstructs from NodeDB
  seeding plus observed traffic after every boot.

### Trust & provenance model

- **Key pin, three layers deep:** an incoming NodeInfo key is checked against
  `copyPublicKeyAuthoritative()` (hot then warm - the same coverage as `updateUser`'s own
  pin), and, failing NodeDB knowledge, against the cache's **own previously cached key**
  (TOFU pin). Mismatches are dropped, never overwritten. A frame advertising _our own_ key
  is dropped outright (impersonation).
- **Key provenance (`keyXeddsaSigned` + `keyManuallyVerified`, combined via `keyProven()`):**
  `keyXeddsaSigned` is set when a frame's XEdDSA signature was router-verified
  (`mp.xeddsa_signed`) or when NodeDB already knew the node as a signer **for the same key**
  (`isVerifiedSignerForKey`). `keyManuallyVerified` is set when the user confirmed possession
  out-of-band (QR / fingerprint), routed via `onNodeKeyCommitted(proven)` and re-seeded from the
  hot store's `is_key_manually_verified` bit at reconcile. Either bit makes `keyProven()` true -
  the predicate the replay gate, eviction tiering, and pubkey-pool callers use. Both are monotonic
  per slot; a changed key resets both.
- **Unsigned-identity gate:** a NodeInfo arriving _unsigned_ from a node we have ever
  verified as a signer - per `NodeDB::isKnownXeddsaSigner()`, which covers hot **and
  warm** tiers - drives no cache, role, or `updateUser()` write. (Warm coverage matters: a
  signer evicted to the warm tier would otherwise be forgeable with its own public key
  until re-heard. The same rule guards `Router::checkXeddsaReceivePolicy`'s
  unsigned-broadcast drop.)
- **Serve gate honesty:** only a genuinely _heard_ NODEINFO frame stamps
  `obsTick`/`hasObserved` - seeding and write-through don't, so a silent node never looks alive
  to the replay path. The sweep clears `hasObserved` to enforce the 6 h serve window. The
  spoofed-reply throttle this gate feeds lives in the module (see
  [traffic_management_module.md](traffic_management_module.md)).

### Consistency with NodeDB (anti-entropy)

Four mechanisms keep this tier a superset of NodeDB's identities. All **merge rather than
overwrite**, so a keyless commit never costs the cache a learned TOFU key.

| Mechanism                                                             | When                        | Role                             |
| --------------------------------------------------------------------- | --------------------------- | -------------------------------- |
| Write-through hooks (`onNodeIdentityCommitted`, `onNodeKeyCommitted`) | every identity/key commit   | immediate upsert                 |
| Reconcile sweep (`reconcileNodeInfoFromNodeDBLocked`)                 | boot seed, then hourly      | re-seed from hot + warm tiers    |
| Membership refresh                                                    | inside the hourly reconcile | re-mark which nodes NodeDB holds |
| Purge hooks (`purgeNode`, `purgeAll`)                                 | node removal / reset        | drop the node from both caches   |

Two details that bite: the reconcile sweep transfers signer verdicts only when **key-matched**;
and membership refresh clears-then-re-marks from both tiers rather than a per-entry NodeDB lookup
each sweep (which would be O(entries x members) under the lock). A keyless warm-tier record still
marks membership (`isMember`) even though it has no `User` to seed - `isMember` is a keep-alive,
independent of `hasFullUser`. Because the re-mark is only hourly, hook-driven additions and
`purgeNode()` removals are immediate, but a **passive** NodeDB eviction may lag membership by up to
an hour.

**Retention:** no timed eviction. Slots die only by LRU displacement on insert, ranked by
trust tiers - members and signer-proven keys are stickiest; the seeding pass additionally
refuses to churn one member out for another (`spareMembers`).

**Key-commit funnel:** every path that writes a remote key into the hot store must route
the write-through. Full-identity commits funnel through `NodeDB::updateUser()`; bare-key
commits (admin-channel learn in `Router::perhapsDecode`, manual verification in
`KeyVerificationModule`) funnel through `NodeDB::commitRemoteKey()`, which carries an
explicit `KeyCommitTrust` provenance (`ManuallyVerified` sets the `keyManuallyVerified` bit in this
cache). Never assign `info->public_key` directly when **learning or rotating a remote
key** - the cache would silently diverge until the next reconcile. (The lone direct write
in `getOrCreateMeshNode()`'s warm-tier re-admission is exempt: it restores a key the warm
tier already holds, which this cache already tracks as a member, so nothing new is learned
and the hourly reconcile re-seeds it even if the packet path had LRU-evicted that slot.)

**Enable gate:** the write-through hooks, the sweep, the packet path, **and the
`copyPublicKey()`/`copyUser()` accessors** all no-op while `moduleConfig.has_traffic_management`
is off, so cache content, maintenance, and reads are keyed to the same condition. This enforces
(not just documents) the corollary that the pubkey-pool superset property holds only while the
module is enabled: a disabled module's frozen cache never feeds PKI resolution or name
rehydration.

### Tick clocks and wrap safety

All TMM timestamps are free-running modular ticks (uint8 or nibble) from `clockMs()`; modular
subtraction is correct only while the true age stays below the counter period, so every clock
needs something to clear expired state before it aliases.

| Clock              | Tick / period  | Window          | Kept honest by                                     |
| ------------------ | -------------- | --------------- | -------------------------------------------------- |
| pos                | 6 min / 25.6 h | <=255 ticks     | 60 s sweep (margin as low as 1 tick at the clamp)  |
| rate               | 5 min / 80 min | <=15 ticks      | sweep + read-time window reset (`isRateLimited()`) |
| unknown            | 1 min / 16 min | 12 ticks        | sweep + read-time window reset                     |
| NodeInfo `obsTick` | 3 min / 12.8 h | 120 ticks (6 h) | sweep only                                         |

`obsTick` is the sharp case: `maintainNodeInfoCacheLocked()` clearing `hasObserved` is the
_sole_ guarantee the 6 h serve gate never reads an aliased stamp. That makes the sweep a
compile-time invariant - guarded by `TMM_HAS_NODEINFO_CACHE` **alone** (never
`TRAFFIC_MANAGEMENT_CACHE_SIZE`, which a variant may zero independently), mirroring `purgeAll()`:
a build that has the cache always has its sweep.

The warm tier is different by design: `WarmNodeStore.last_heard` is an **absolute** unix-seconds
timestamp (128 s quantised), so it cannot wrap until 2106 and needs no sweep - the TMM caches
chose 1-byte ticks instead to stay at 10 B/entry across up to 2048 entries.

### Direct-response behavior

How this cache's identities are served as spoofed direct NodeInfo replies - the serve gates,
the per-requester/per-target/global throttle, and the "throttled forwards, not dropped"
behaviour - is documented with the module in
[traffic_management_module.md](traffic_management_module.md).

---

## Property matrix

Side-by-side view of what each store actually holds ("-" = not held). Details and
rationale live in the per-store sections above.

| Property                           | 1. Hot store (`NodeInfoLite`)           | 2. Warm tier (`WarmNodeEntry`)                            | 3. NodeInfo cache (`NodeInfoPayloadEntry`)                    | 4. Unified cache (`UnifiedCacheEntry`)               |
| ---------------------------------- | --------------------------------------- | --------------------------------------------------------- | ------------------------------------------------------------- | ---------------------------------------------------- |
| Node number                        | yes                                     | yes                                                       | yes (0 = free slot)                                           | yes (0 = free slot)                                  |
| Names + user id                    | yes (flattened fields)                  | -                                                         | yes (full `User`, when `hasFullUser`)                         | -                                                    |
| Public key (32 B)                  | yes (authoritative)                     | yes (keyed entries)                                       | yes (TOFU or proven; pinned against tiers 1-2)                | -                                                    |
| Key provenance - XEdDSA signed     | `HAS_XEDDSA_SIGNED` bitfield bit        | 1 signer bit (shared with `last_heard`)                   | `keyXeddsaSigned`                                             | -                                                    |
| Key provenance - manually verified | `IS_KEY_MANUALLY_VERIFIED` bitfield bit | - (not carried; collapsed into protected category)        | `keyManuallyVerified`                                         | -                                                    |
| Device role                        | `role` field                            | 4-bit role (metadata steal)                               | inside the cached `User`                                      | 4-bit role in count-byte top bits (final fallback)   |
| Recency                            | `last_heard` (unix secs)                | `last_heard` (unix secs, 128 s quantised)                 | `obsTick` (3 min modular tick) + `hasObserved`                | pos/rate/unknown modular ticks                       |
| Position / telemetry               | via satellite copy-out accessors        | -                                                         | -                                                             | 8-bit position _fingerprint_ only (dedup)            |
| Protected / favorite               | bitfield flags                          | 2-bit protected category                                  | - (`isMember` keep-alive instead)                             | -                                                    |
| Routing hint (`next_hop`)          | yes (persisted field)                   | -                                                         | -                                                             | ACK-confirmed relay byte (preloaded from tier 1)     |
| Direct-reply metadata              | -                                       | -                                                         | `sourceChannel`, `decodedBitfield` (+ `hasDecodedBitfield`)   | -                                                    |
| Traffic-shaping counters           | -                                       | -                                                         | -                                                             | rate + unknown counts, pos fingerprint               |
| Entry size                         | largest (full struct)                   | 40 B exact                                                | ~`sizeof(User)`+8, platform-padded (no size assert by design) | 10 B exact                                           |
| Capacity                           | `MAX_NUM_NODES` (250/120/10)            | `WARM_NODE_COUNT` (~100)                                  | `kNodeInfoCacheEntries` (2000)                                | `TRAFFIC_MANAGEMENT_CACHE_SIZE` (2048/500/400/250/0) |
| Persistence (durable)              | LittleFS (node DB file)                 | raw flash ring (nRF52840) or LittleFS (`/prefs/warm.dat`) | none (rebuilt from seed + traffic)                            | none                                                 |
| Storage (runtime)                  | heap                                    | heap (PSRAM on ESP32 when available)                      | PSRAM on hardware; heap in native tests                       | PSRAM when available, else heap                      |

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
 3. TMM NodeInfo cache (extended)    full User payloads + TOFU/proven keys, ephemeral
        │ miss                        (role-only: 4-bit role in the unified cache)
        ▼
      defaults (no key; role = CLIENT)
```

The unified cache (§3) sits beside this chain rather than in it: it is traffic-shaping
state keyed by the same NodeNum, whose role bits act as the final role fallback when all
three identity tiers miss.
