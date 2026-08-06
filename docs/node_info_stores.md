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

**Memory classes.** The warm tier (§2) and unified cache (§3) size themselves from
`MESHTASTIC_MEM_CLASS` (`src/memory/MemClass.h`), which ranks a build by _usable app heap after
platform overheads_ (SoftDevice, WiFi+BLE stacks) rather than by raw RAM or chip family. The hot
store (§1) is flash-shaped and the NodeInfo cache (§4) is present-or-absent, so neither is classed:

| Class  | Heap                  | Parts                                        |
| ------ | --------------------- | -------------------------------------------- |
| LARGE  | PSRAM or host         | ESP32-S3 with PSRAM, portduino/native        |
| MEDIUM | ~250-500 KB, no PSRAM | ESP32-S3/C6/P4 without PSRAM                 |
| SMALL  | ~100-250 KB           | classic ESP32/S2/C3, nRF52840, RP2040/RP2350 |
| TINY   | <32 KB                | STM32WL                                      |

An unclassified chip lands in SMALL on purpose: small caches are a recoverable default, an
exhausted heap is not. Where a capacity table names a specific part beside these classes, that
part is deliberately class-deviant and the reason is given under the table.

---

## 1. NodeDB hot store (authoritative)

- **What:** the classic `meshNodes` array of `meshtastic_NodeInfoLite` - full identity as
  flattened fields (names, role, public key, bitfield flags such as `HAS_XEDDSA_SIGNED`;
  position/telemetry live in satellite stores reached via copy-out accessors, not nested
  members). Everything else in this document is a cache or a fallback for it.
- **Eviction:** oldest non-protected node when full (`getOrCreateMeshNode`). On eviction
  the node's essentials are **absorbed into the warm tier** (see §2); on re-admission the
  warm record is rehydrated back (`take()`), including the XEdDSA-signed bit.
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

**Capacity** - `MAX_NUM_NODES`:

| ESP32-S3        | Native (portduino) | nRF52840, generic ESP32 | STM32WL |
| --------------- | ------------------ | ----------------------- | ------- |
| 250 / 200 / 100 | 200, configurable  | 120                     | 10      |

This one is flash-shaped rather than heap-shaped, so it is unclassed: `nodes.proto` has to fit the
filesystem. The fixed-cap platforms get their value from `mesh-pb-constants.h`; the 120 covers
nRF52840 plus generic ESP32 including C3, and is what keeps `nodes.proto` inside the stock 28 KB
LittleFS.

**Two platforms do not take their cap from that header, and neither is a compile-time constant:**

- **ESP32-S3** picks a tier at boot from the flash chip size (>=15 MB / >=7 MB / smaller).
- **Native/portduino** resolves it from _runtime_ config:
  `variants/native/portduino{,-buildroot}/variant.h` define `MAX_NUM_NODES portduino_config.MaxNodes`,
  default **200** (`PortduinoGlue.h`), overridable per-host with `General: MaxNodes` in the YAML.
  Because `variant.h` is reached first, the `ARCH_PORTDUINO` branch of `mesh-pb-constants.h` never
  fires - it is `#error`-guarded so it can no longer be misread as the native cap.

Do not grep `mesh-pb-constants.h` for the native number: the protected-node cap derives from
`MAX_NUM_NODES` (`numProtectedNodes() < MAX_NUM_NODES - 2`), so a wrong reading gives a wrong cap
(248 instead of 198) and makes a genuinely saturated database look impossible.

The separate `250` in `NodeDB::getMaxNodesAllocatedSize()` is `NODEDB_MIGRATION_LOAD_CEILING`, a
decode allowance for files written by larger-cap firmware. It is not a cap on this build.

## 2. Warm tier - `WarmNodeStore` (NodeDB-owned)

- **What:** the "long-tail" second tier. When a node ages out of the hot store, a minimal
  record survives so DMs keep encrypting: the key is expensive to re-learn; everything
  else rebuilds from traffic in seconds.
- **Entry:** exactly 40 bytes - `num(4) | last_heard(4) | public_key(32)`. The low 7 bits
  of `last_heard` are omitted, and replaced with metadata (role: 4 bits, protected
  category: 2, XEdDSA-signed bit: 1), leaving ~128 s recency resolution - plenty for LRU ranking.
- **Capacity:** `WARM_NODE_COUNT` (100 on constrained parts; platform-tiered).
- **Eviction:** LRU by `last_heard`, with keyed entries outranking keyless; keyless
  candidates never displace keyed entries.
- **Persistence:** nRF52840 uses a 12 KB raw-flash record-ring below LittleFS
  (append/replay/compact); everywhere else `/prefs/warm.dat` (LittleFS).
- **Membership invariant:** a node lives in the hot **XOR** warm tier. `take()` removes
  the warm record when the node is re-admitted hot, restoring role/protected/XEdDSA-signed bits.

**Capacity** - `WARM_NODE_COUNT` (`mesh-pb-constants.h`):

| LARGE | MEDIUM | RP2040 / RP2350 | nRF52840 | SMALL | TINY |
| ----- | ------ | --------------- | -------- | ----- | ---- |
| 2000  | 150    | 150             | 100      | 100   | 0    |

TINY's 0 disables the tier outright. At 40 B/entry, LARGE costs ~80 KB and lives in PSRAM, MEDIUM
~6 KB of heap. Both named parts are class-deviant on purpose: RP2040/RP2350 is bounded so the
`warm.dat` write fits the 8 s watchdog (#10746) rather than by RAM, and nRF52840 dropped from 200 to
100 because its RAM cache is calloc'd from the ~115 KB heap arena shared with SoftDevice, which
2.8.0 field reports showed at 99% use.

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
- **Eviction:** linear scan; insertion on a full cache evicts the stalest entry,
  preferring to keep entries with a `next_hop` hint **or** a cached special (non-`CLIENT`)
  role - the long-tail state this cache exists to retain (`findOrCreateEntry`'s `preferred`
  test covers both, not just `next_hop`).
- **Persistence:** none - PSRAM (or heap) only, rebuilt from traffic.

**Capacity** - `TRAFFIC_MANAGEMENT_CACHE_SIZE` (`mesh-pb-constants.h`), variant-overridable:

| LARGE | MEDIUM | SMALL | nRF52840 | `HAS_TRAFFIC_MANAGEMENT=0` |
| ----- | ------ | ----- | -------- | -------------------------- |
| 2048  | 500    | 400   | 250      | 0                          |

At 10 B/entry that is ~5 KB on MEDIUM and ~2.5 KB on nRF52840, which is class-deviant for the same
heap reason as the warm tier (its class would give 400); 250 entries still tracks over 2x the
120-node hot store, and LRU victim recycling absorbs busier meshes.

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
- **Persistence:** none - this tier is deliberately ephemeral; it reconstructs from NodeDB
  seeding plus observed traffic after every boot.

**Capacity** - `kNodeInfoCacheEntries` (`TrafficManagementModule.h`), gated by
`TMM_HAS_NODEINFO_CACHE`:

| ESP32 + PSRAM | Native unit-test builds | Everything else |
| ------------- | ----------------------- | --------------- |
| 2000          | 2000                    | not compiled    |

Not class-tiered: the array is either compiled or it isn't. ESP32+PSRAM is the production home (in
PSRAM); native test builds put the same 2000 entries on the plain heap so the trust and retention
paths run in CI. Linear scan in every build - NodeInfo traffic is low-rate.

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
trust tiers - members and key-proven keys are stickiest; the seeding pass additionally
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

This cache's `obsTick` recency stamp, like the unified cache's pos/rate/unknown stamps, is a
free-running modular tick rather than an absolute time, and depends on the maintenance sweep to
clear expired state before it aliases. The per-clock periods, windows, and what keeps each honest
are documented with the module in
[traffic_management_module.md](traffic_management_module.md#tick-clocks-and-wrap-safety). The sharp
case for this tier is `obsTick`: the sweep clearing `hasObserved` is the _sole_ guarantee the 6 h
serve gate never reads an aliased stamp, which is why it is a compile-time invariant guarded by
`TMM_HAS_NODEINFO_CACHE` alone.

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

| Property                   | 1. Hot store                       | 2. Warm tier                   | 3. NodeInfo cache                  | 4. Unified cache                |
| -------------------------- | ---------------------------------- | ------------------------------ | ---------------------------------- | ------------------------------- |
| Struct                     | `NodeInfoLite`                     | `WarmNodeEntry`                | `NodeInfoPayloadEntry`             | `UnifiedCacheEntry`             |
| Node number                | yes                                | yes                            | yes (0 = free)                     | yes (0 = free)                  |
| Names + user id            | yes (flattened)                    | -                              | yes (full `User`)                  | -                               |
| Public key (32 B)          | yes (authoritative)                | yes (keyed entries)            | yes (TOFU/proven; pinned)          | -                               |
| Key source - XEdDSA signed | `HAS_XEDDSA_SIGNED` bit            | 1 bit (in `last_heard`)        | `keyXeddsaSigned`                  | -                               |
| Key source - manual scan   | `IS_KEY_MANUALLY_VERIFIED` bit     | - (not carried)                | `keyManuallyVerified`              | -                               |
| Device role                | `role` field                       | 4-bit role (metadata steal)    | in cached `User`                   | 4-bit role (final fallback)     |
| Recency                    | `last_heard` (unix s)              | `last_heard` (128 s quant.)    | `obsTick` (3 min) + `hasObserved`  | modular ticks                   |
| Position / telemetry       | satellite accessors                | -                              | -                                  | 8-bit pos fingerprint (dedup)   |
| Protected / favorite       | bitfield flags                     | 2-bit protected category       | - (`isMember` instead)             | -                               |
| Routing hint (`next_hop`)  | yes (persisted)                    | -                              | -                                  | ACK-confirmed relay byte        |
| Direct-reply metadata      | -                                  | -                              | `sourceChannel`, `decodedBitfield` | -                               |
| Traffic-shaping counters   | -                                  | -                              | -                                  | rate + unknown counts, pos fp   |
| Entry size                 | largest (full struct)              | 40 B exact                     | ~`sizeof(User)`+8 (padded)         | 10 B exact                      |
| Capacity (symbol)          | `MAX_NUM_NODES`                    | `WARM_NODE_COUNT`              | `kNodeInfoCacheEntries`            | `TRAFFIC_MANAGEMENT_CACHE_SIZE` |
| Capacity (entries)         | 250/200/120/100/10 (native: 200\*) | ~100                           | 2000                               | 2048/500/400/250/0              |
| Persistence (durable)      | LittleFS (node DB)                 | flash ring (nRF52840)/LittleFS | none (rebuilt)                     | none                            |
| Storage (runtime)          | heap                               | heap / PSRAM (ESP32)           | PSRAM (hw) / heap (test)           | PSRAM / heap                    |

\* Native/portduino is not a compile-time value: it is `portduino_config.MaxNodes`; the host default
is 200, settable per-host via `General: MaxNodes`, and the WASM build overrides it to 80
(`wasm_config_apply()`). See the hot-store capacity section above.

## How a lookup falls through the tiers

```text
identity/role/key consumer
        │
        ▼
 1. hot store (NodeInfoLite)         full identity, authoritative
        │ miss
        ▼
 2. warm tier (WarmNodeStore)        key + role/protected/XEdDSA-signed bits, persisted
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
