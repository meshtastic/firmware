# NextHop direct-message reliability on dense meshes - findings & plan

**Status:** Implemented - mitigations and tests in `PR3-tmm-nexthop`
**Date:** 2026-06-13
**Area:** `src/mesh` router stack (`NextHopRouter`, `ReliableRouter`, `FloodingRouter`, `Router`, `NodeDB`, `PacketHistory`)
**Constraint:** No over-the-air / wire-format changes - `next_hop` and `relay_node` stay 1 byte, no `PacketHeader` changes, no breaking protobuf changes. All new state is RAM-only.

This document captures the analysis and the proposed mitigations so the work can be
continued on this branch by anyone. It is intentionally code-grounded (file:line
references throughout) and standalone - you should not need the original investigation
context to pick it up.

---

## TL;DR

NextHop routing for direct messages (DMs) is unreliable on dense meshes. The headline
cause is the **birthday problem**: `next_hop` and `relay_node` are each a single byte
(the last byte of a 32-bit node number), so on a mesh of N nodes the probability that
two share the same byte hits ~50% at **~19 nodes** and is near-certain by 50-100. But
there are **other, equally important issues**: that single byte is trusted blindly at
five different code sites, learned routes **never decay**, routes are learned from the
**reverse (ACK) path** (asymmetric-link hazard), and collision-driven spurious
rebroadcasts **amplify congestion** exactly when the mesh is busy.

Because we can't widen the on-wire field, the fix is **interpretation-side** ("don't
trust a byte that doesn't map to a unique reachable neighbor - flood instead") plus
**recovery-side** ("decay stale/failing routes so they get re-discovered"). Four
mitigations, M1-M4, all RAM-only. The net behavioral change: on dense/mobile meshes a
DM that today silently misroutes or black-holes instead falls back to managed flooding
(which still delivers) and re-learns a fresh route quickly. Sparse-mesh happy paths are
unchanged.

---

## How NextHop routing works today (mechanics)

Inheritance chain: `Router` → `FloodingRouter` → `NextHopRouter` → `ReliableRouter`.

**The single-byte identifiers.** Both routing bytes come from one helper:

```cpp
// src/mesh/NodeDB.h:255
uint8_t getLastByteOfNodeNum(NodeNum num) { return (uint8_t)((num & 0xFF) ? (num & 0xFF) : 0xFF); }
```

It projects a 32-bit node number onto 255 values (`0x00` is remapped to `0xFF` so it
never collides with the `0`-valued sentinels `NO_NEXT_HOP_PREFERENCE` / `NO_RELAY_NODE`,
`src/mesh/MeshTypes.h:44-46`). `next_hop` and `relay_node` in the packet header are
`uint8_t` (`src/mesh/mesh.pb.h`, comments "Last byte of the node number…"). The learned
route stored per destination, `meshtastic_NodeInfoLite::next_hop`, is also a single byte
(`src/mesh/generated/meshtastic/deviceonly.pb.h:83`).

**Sending a DM** - `NextHopRouter::send` (`src/mesh/NextHopRouter.cpp:23`):

1. `p->relay_node = getLastByteOfNodeNum(getNodeNum())` (mark ourselves as relayer).
2. `p->next_hop = getNextHop(p->to, p->relay_node)` (`src/mesh/NextHopRouter.cpp:192`):
   look up `nodeDB->getMeshNode(to)->next_hop`; return it unless it equals the relayer
   byte; otherwise `NO_NEXT_HOP_PREFERENCE` (→ flood).

**Relaying** - `NextHopRouter::perhapsRebroadcast` (`src/mesh/NextHopRouter.cpp:133`):
rebroadcast iff `next_hop == NO_NEXT_HOP_PREFERENCE` (flood) **or**
`next_hop == getLastByteOfNodeNum(getNodeNum())` (we are the addressed next hop)
(`:147`). Each node only ever compares against **its own** byte.

**Learning** - `NextHopRouter::sniffReceived` (`src/mesh/NextHopRouter.cpp:89`): on an
ACK/reply (`request_id`/`reply_id` set), if the relayer of the ACK was also a relayer of
the original packet (validated via `PacketHistory::checkRelayers`), set
`origTx->next_hop = p->relay_node` (`:114`). I.e. the **forward** next-hop is learned
from the **reverse** path's relayer.

**Retransmission / fallback** - `NextHopRouter::doRetransmissions`
(`src/mesh/NextHopRouter.cpp:284`). Budgets: `NUM_RELIABLE_RETX=3` (originator: initial

- 2 retries), `NUM_INTERMEDIATE_RETX=2` (relayer: 1 retry). On the **last** retry
  (`numRetransmissions==1`) it resets `next_hop` to `NO_NEXT_HOP_PREFERENCE` on the packet
  **and** clears `sentTo->next_hop` in NodeDB, then floods (`:313-321`). Retransmit timing
  comes from `iface->getRetransmissionMsec`, whose contention window **grows with channel
  utilization** (`src/mesh/RadioInterface.cpp` `getTxDelayMsec`/`getTxDelayMsecWeighted`).

**Dedup / relayer history** - `PacketHistory` (`src/mesh/PacketHistory.cpp`): a bounded
ring (`PACKETHISTORY_MAX = max(MAX_NUM_NODES*2, 100)`, 20 B/record) keyed by
`(sender,id)`, tracking up to `NUM_RELAYERS=6` relayer **bytes** per packet in
`relayed_by[]`. `wasRelayer` (`:490`) and `checkRelayers` (`:517`) match bytes against
that array.

---

## Root-cause analysis

### 1. The single byte is trusted blindly at five sites (the birthday problem)

| #   | Site                             | File:line                   | Failure on collision                                                                                                      |
| --- | -------------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| 1   | Rebroadcast self-check           | `NextHopRouter.cpp:147`     | A remote "impostor" node sharing the intended next-hop's byte also rebroadcasts → wasted airtime / congestion.            |
| 2   | Route learning                   | `NextHopRouter.cpp:111-114` | Stores an ambiguous byte as the route; later resolves to the wrong physical node.                                         |
| 3   | Relayer validation               | `PacketHistory.cpp:490-538` | `wasRelayer(byte)` returns true for the wrong node → mis-validated ACK / mis-learn.                                       |
| 4   | Favorite-router hop preservation | `Router.cpp:120-145`        | **First** NodeDB node whose last byte matches wins - non-deterministic; can preserve hops for the wrong relay (hop leak). |
| 5   | Send-path lookup                 | `NextHopRouter.cpp:192-207` | Emits a byte that may address the wrong node; no check it still maps to a reachable neighbor.                             |

Collision math (uniform last byte over 255 buckets): P(collision) ≈ 50% at ~19 nodes,

> 99% by ~75 nodes. Dense meshes are squarely in the "always colliding" regime.

### 2. Stale routes never decay

The learned `next_hop` byte is cleared only on the **current DM's** last retry
(`NextHopRouter.cpp:313-321`). A route learned hours ago that has since gone dead is
still trusted on the **next** DM's first attempt - which on a congested mesh is also the
slowest attempt. Result: silent black-hole at a dead hop until the retransmission budget
drains, then a late flood. Intermediate nodes hold stale routes indefinitely.

### 3. Reverse-path (asymmetric-link) learning

`origTx->next_hop` is learned from the ACK's relayer (`NextHopRouter.cpp:110-114`) - the
**reverse** direction. RF links are frequently asymmetric, so the best reverse relay can
be a poor forward relay. Worse, the next reverse ACK immediately re-learns the same bad
hop, so the route **flaps** back to the bad value even after a failure reset.

### 4. Congestion amplification

Collision-driven impostor rebroadcasts (issue 1) add airtime; the contention window
grows with channel utilization, so retransmit intervals **lengthen** exactly when the
mesh is busy. The 3-try reliable budget can then expire before delivery. On dense
meshes, efficiency _is_ reliability.

### Note: pubkey-derived node numbers (develop / 2.8) - does not change the plan

develop derives the node number from the public key:
`my_node_num = crc32Buffer(public_key)` (`src/mesh/NodeDB.cpp:481`), re-derived on key
change in `createNewIdentity()` (`src/mesh/NodeDB.cpp:3113`). This **reinforces** the
plan rather than changing it:

- **Birthday problem unchanged and now textbook-exact.** CRC32 mixes well → the last
  byte is uniformly distributed over 256 values. Derivation adds no wire bits.
- **Node numbers are now immutable / identity-bound.** Pre-2.8 `pickNewNodeNum()` could
  renumber a node to dodge a conflict; now the number is fixed by the key, so a last-byte
  collision **cannot be resolved operationally by renumbering** → M1/M2/M3 become _more_
  necessary.
- **Resolver gets cleaner inputs.** Stable node numbers keep a learned byte bound to one
  identity (good for M3 freshness). `createNewIdentity()` retires the old entry by marking
  it **ignored** and clearing its pubkey (`src/mesh/NodeDB.cpp:3123-3125`), which M1's
  candidate gate already skips - so key rotation can't pollute resolution.
- **No wire-free disambiguation unlocked.** A receiver still gets only 1 byte and cannot
  recover which full node number a colliding value meant - so "detect ambiguity → flood"
  remains the correct strategy.

---

## Proposed mitigations

Key insight for all of M1/M2: **a 1-byte ID only needs to be unique among a node's
direct neighbors / plausible relays, not the whole mesh.** That candidate set is small
(typically 5-15), so a byte usually resolves unambiguously there; when it doesn't, fall
back to the _safe_ behavior (flood / decrement / don't-learn).

### M1 - Ambiguity-aware last-byte resolution (new NodeDB primitive)

New types + methods in `src/mesh/NodeDB.h` (near line 255) / `src/mesh/NodeDB.cpp`
(near `getMeshNode`, ~2936):

```cpp
enum class LastByteResolution : uint8_t { None, Unique, Ambiguous };
struct ResolvedNode { LastByteResolution status = LastByteResolution::None; NodeNum num = 0; };

// Resolve a single on-wire last-byte to a unique full NodeNum among relevant candidates.
ResolvedNode resolveLastByte(uint8_t lastByte, bool requireDirectNeighbor);
// Convenience: true iff exactly one relevant candidate (Ambiguous and None both -> false = SAFE).
bool resolveUniqueLastByte(uint8_t lastByte, bool requireDirectNeighbor, NodeNum *outNum = nullptr);
```

- **One linear pass** over `meshNodes`, reusing `getNumMeshNodes()`/`getMeshNodeByIndex()`,
  the bitfield helpers (`nodeInfoLiteIsFavorite/HasUser/IsIgnored`), `sinceLastSeen()`,
  and `getLastByteOfNodeNum()`. **Early-exit** on the 2nd match (return `Ambiguous`).
- **Guard:** `if (lastByte == 0) return {None, 0};` (covers `NO_RELAY_NODE` / MQTT-invalid).
- **Candidate gate** (skip): `num == getNodeNum()` (never resolve to ourselves), `num == 0`,
  `num == NODENUM_BROADCAST`, `nodeInfoLiteIsIgnored`. Then match
  `getLastByteOfNodeNum(node->num) == lastByte` (cheapest test last, mirroring `Router.cpp:119`).
- **Relevance gate:**
  - `requireDirectNeighbor == true` (strict, for SEND): `has_hops_away && hops_away == 0`
    **and** `sinceLastSeen(node) < NEXTHOP_NEIGHBOR_FRESH_SECS`.
  - `requireDirectNeighbor == false` (lenient, for learn / hop-preserve): accept if direct
    neighbor **or** `nodeInfoLiteIsFavorite` **or** role ∈ {ROUTER, ROUTER_LATE, CLIENT_BASE}.
- **No tie-break.** A collision must return `Ambiguous` - picking "best SNR" would
  resurrect the silent-misroute bug. (Deliberate non-goal; document in code.)

New constant in `src/mesh/MeshTypes.h` (near line 44):
`#define NEXTHOP_NEIGHBOR_FRESH_SECS (60 * 60 * 2)` (mirrors `NUM_ONLINE_SECS`).

### M2 - Only route on bytes that resolve to a unique, reachable neighbor

In `getNextHop` (`src/mesh/NextHopRouter.cpp:192-207`), after the existing split-horizon
check (`node->next_hop != relay_node`), require the stored byte to resolve to a **unique,
currently-fresh direct neighbor**; else flood:

```cpp
if (node->next_hop != relay_node) {
    ResolvedNode r = nodeDB->resolveLastByte(node->next_hop, /*requireDirectNeighbor=*/true);
    if (r.status == LastByteResolution::Unique) return node->next_hop;
    LOG_WARN("Next hop 0x%x for 0x%x %s -> flood", node->next_hop, to,
             r.status == LastByteResolution::Ambiguous ? "ambiguous among neighbors" : "no longer a neighbor");
    return std::nullopt;
}
```

This self-heals when a neighbor goes away (unicast-into-a-void becomes a flood). It
applies to originating, relaying, and retrying, since all route through `getNextHop`.

Apply M1's safe fallback at the other sites:

- **Learning** (`NextHopRouter.cpp:111-114`): gate `origTx->next_hop = p->relay_node` on
  `resolveUniqueLastByte(p->relay_node, /*direct=*/false)`. Ambiguous/unknown → don't
  learn (leave route unset → flood).
- **Favorite-router preservation** (`Router.cpp:120-145`): replace the "first match wins"
  loop with `resolveUniqueLastByte(p->relay_node, /*direct=*/false)` + a re-check that the
  resolved node is favorite/has_user/router. Ambiguous/none/not-favorite → **decrement**
  (safe). Net: removes one full DB scan, adds one resolver scan (wash).

**Left unchanged, by design (document why in code):**

- **Site 1** rebroadcast self-check (`NextHopRouter.cpp:147`) and self-identity checks
  (`ReliableRouter.cpp:127`): a node matches its **own** byte - no DB resolution helps. A
  remote impostor sharing the intended next-hop's byte will still rebroadcast. M1/M2
  shrink the blast radius by reducing how often an ambiguous byte is ever stored or
  originated; a true fix needs a wider field (out of scope). **This is the one residual
  the plan cannot fully close.**
- **Site 3** `wasRelayer`/`checkRelayers` (`PacketHistory.cpp:490-538`): intentionally
  byte-domain (both sides are on-wire bytes); the consumer (learning) is now hardened.
  Add a one-line comment; do not change.

### M3 - Route freshness / failure memory (RAM table on NextHopRouter)

A bounded, LRU-evicted table keyed by destination, mirroring `PacketHistory`'s
reuse-oldest discipline (not an unbounded map) to cap RAM.

`src/mesh/NextHopRouter.h` (near `pending`, line 99):

```cpp
struct RouteHealth {
    NodeNum dest = 0;                  // 0 == empty slot
    uint32_t learnedAtMsec = 0;        // millis() at last (re)learn; rollover-aware
    uint8_t consecutiveFailures = 0;
    uint8_t lastNextHop = NO_NEXT_HOP_PREFERENCE; // byte this health refers to
};
static constexpr uint8_t ROUTE_HEALTH_MAX = 32;   // ~384B; drop to 16 if RAM-tight
RouteHealth routeHealth[ROUTE_HEALTH_MAX] = {};
// Helpers take `now` (pure/testable): findRouteHealth, getOrAllocRouteHealth,
// noteRouteLearned, noteRouteSuccess, noteRouteFailure, isRouteStale, clearRouteHealth
```

Policy:

| Constant                  | Value  | Rationale                                                                                                                                              |
| ------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ROUTE_TTL_MSEC`          | 30 min | Survives a normal conversation; re-discovers a moved node within a telemetry interval.                                                                 |
| `ROUTE_FAILURE_THRESHOLD` | 3      | 1-2 consecutive failures are transient LoRa collisions; 3 to the same hop = dead. Accumulates **across** DMs (independent of the per-DM 3-try budget). |

`isRouteStale(h, now)` = `(now - h.learnedAtMsec) >= ROUTE_TTL_MSEC || h.consecutiveFailures >= ROUTE_FAILURE_THRESHOLD`.
All age math uses **unsigned subtraction** (rollover-safe, matching
`PacketHistory.cpp:364`); treat `learnedAtMsec == 0` as "set now".

Wiring (as built - `src/mesh/NextHopRouter.cpp`, `src/mesh/ReliableRouter.cpp`):

- `getNextHop`: if a health record matches the stored byte and `isRouteStale`, clear
  `node->next_hop` (NodeDB) **and** `clearRouteHealth`, return `nullopt` (flood). No
  record yet (cold path, first DM after boot) → trust NodeDB, but the M2 strict-neighbor
  gate still applies.
- `sniffReceived` learn: gate the write through `resolveUniqueLastByte` (M2), then
  `noteRouteLearned(p->from, p->relay_node, millis())` - resets `consecutiveFailures`
  **only if the hop changed** (anti-flap for asymmetric re-learn); otherwise just refreshes
  `learnedAtMsec`. (No success signal is taken on the intermediate reverse-pass: an ACK
  merely passing through us is not proof that _we_ delivered, and resetting failures there
  would reintroduce the asymmetric flap.)
- `doRetransmissions`: on the last-retransmission branch (`numRetransmissions == 1`, the
  point a directed delivery has gone un-ACKed for both originator and intermediate) →
  `noteRouteFailure(to)`, then the existing NodeDB `next_hop` reset + flood. We deliberately
  do **not** `clearRouteHealth` here: keeping the record is what lets the failure count
  accumulate across DMs so a flapping reverse-path-relearned dead hop eventually ages out.
- `ReliableRouter::sniffReceived` ACK path → `noteRouteSuccess(getFrom(p), millis())`
  (an end-to-end ACK addressed to us is genuine forward-delivery proof; clears failures and
  refreshes freshness). `noteRouteSuccess`/`noteRouteFailure` are no-ops when no record
  exists, so flood-only destinations never pollute the table.

**Reconciliation (no double-handling):** `doRetransmissions` owns _in-flight_ failure of
the current DM (reset NodeDB `next_hop` + flood, and bump the cross-DM failure counter);
`getNextHop` owns _between-DM_ staleness (TTL or failure-threshold → flood + clear). The
only place that erases a health record is the `getNextHop` decay path; the retransmission
path leaves it intact so the counter survives a reverse-path re-learn.

### M4 - Earlier flood for unverified routes (gated, off by default)

Compile-gated so healthy sparse meshes are untouched. **Default is off** - the define
lives in `NextHopRouter.h` and must be flipped to measure:
`#define NEXTHOP_EARLY_FLOOD_ON_UNVERIFIED 1`.

In `doRetransmissions`, the directed-retry `else` branch: if the route is **not verified**
(`!findRouteHealth(to) || consecutiveFailures > 0 || isRouteStale`), reset `next_hop` and
flood on this attempt instead of spending another directed try. A **verified** route
(record present, `consecutiveFailures == 0`, within TTL - i.e. recently ACKed) takes the
unchanged directed-retry path, so the sparse-mesh happy path is untouched. Trade-off:
airtime ↔ latency; the gate ensures we never pay the flood cost on a proven route, only on
one we already distrust. Off by default precisely so it can be A/B-measured on the
simulator before broad enable.

---

## Files to modify

| File                                        | Change                                                                                                                                               |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/mesh/MeshTypes.h`                      | `NEXTHOP_NEIGHBOR_FRESH_SECS`, `ROUTE_TTL_MSEC`, `ROUTE_FAILURE_THRESHOLD`, `NEXTHOP_EARLY_FLOOD_ON_UNVERIFIED`                                      |
| `src/mesh/NodeDB.h` / `src/mesh/NodeDB.cpp` | `LastByteResolution`, `ResolvedNode`, `resolveLastByte`, `resolveUniqueLastByte`                                                                     |
| `src/mesh/NextHopRouter.h`                  | `RouteHealth` + array + helpers; `#ifdef PIO_UNIT_TESTING public:` for helpers and `getNextHop`                                                      |
| `src/mesh/NextHopRouter.cpp`                | `getNextHop` (M2 gate + M3 decay); `sniffReceived` (learn gate + health seed + success); `doRetransmissions` (failure counting + M4); comment site 1 |
| `src/mesh/Router.cpp`                       | `shouldDecrementHopLimit` → resolver + favorite/router re-check                                                                                      |
| `src/mesh/ReliableRouter.cpp`               | ACK path → `noteRouteSuccess`                                                                                                                        |
| `test/test_nexthop_routing/test_main.cpp`   | **new** unit suite (auto-built under `[env:native]`)                                                                                                 |

**Reuse, don't reinvent:** `getLastByteOfNodeNum`, `sinceLastSeen`, the bitfield helpers,
`getMeshNodeByIndex`/`getNumMeshNodes`, PacketHistory's reuse-oldest eviction shape, and
`MockNodeDB::addTestNode` (from `test/test_hop_scaling/test_main.cpp`).

---

## Edge cases

- **`0x00`↔`0xFF` projection:** the resolver compares via `getLastByteOfNodeNum` on both
  sides, so a `…00` node and a `…FF` node correctly collide on `0xFF` → `Ambiguous`. Test
  explicitly.
- **MQTT packets:** `relay_node`/`next_hop` are forced invalid when `hop_start == 0`
  (`src/mesh/RadioLibInterface.cpp:603-605`) → byte 0 → resolver `None` → don't learn
  (correct).
- **`has_hops_away == false`** nodes are excluded from the strict gate (never fabricate a
  Unique neighbor for M2); admitted to the lenient gate only via favorite/router role.
  Safe; self-corrects once `hops_away` is learned.
- **Self / broadcast:** the resolver skips `getNodeNum()` and `NODENUM_BROADCAST`;
  `getNextHop` already early-returns for broadcast.
- **Perf:** M2 adds one O(N) resolver scan per directed send/relay (early-exit on the 2nd
  match), cheaper than the crypto already on that path; site-4 is a wash. If ever hot, a
  future 256-entry last-byte index is the optimization (not now - RAM).

---

## Verification (all tiers)

### 1. Native unit tests - new `test/test_nexthop_routing/test_main.cpp`

`pio test -e native -f test_nexthop_routing`; on macOS `./bin/test-native-docker.sh -f test_nexthop_routing`.
Design the RouteHealth helpers to take `now` as a parameter so the 30-min TTL logic is
testable without a clock mock.

- **Resolver:** None / Unique / **Ambiguous (birthday collision)** / strict-excludes-stale /
  strict-excludes-far / lenient-includes-favorite-router / lenient-collision / skips-self /
  skips-ignored / **`0x00`↔`0xFF` collision** / early-exit.
- **`getNextHop`:** unique→byte, **ambiguous→nullopt**, stale-neighbor→nullopt,
  split-horizon (relay==next_hop)→nullopt, broadcast→nullopt.
- **RouteHealth:** TTL boundary, **rollover** (learn near `0xFFFFFFFF`, check after wrap),
  failure threshold, success-resets, **re-learn-same-hop keeps fails (anti-flap)**,
  re-learn-new-hop resets, LRU eviction bound, clear.
- **Site-4:** preserve on unique favorite router; **decrement on two colliding favorites**;
  decrement when the resolved node is not a favorite.
- **Sparse-mesh regression:** all-distinct last bytes → every resolve Unique, `getNextHop`
  returns the stored byte unchanged (proves no happy-path change).
- Re-run `test_packet_history` and `test_hop_scaling` for no regression.

### 2. portduino SimRadio simulator

`pio run -e native && ./bin/test-simulator.sh`. Best vehicle for the **intermediate-node**
path the 2-device bench can't reach. Line topology A - B - C: establish A→C (B learns a
directed route), stop B relaying that dest, confirm A re-discovers via flood within
`ROUTE_FAILURE_THRESHOLD` and that B's `noteRouteFailure`/`clearRouteHealth` fires (visible
via the `LOG_INFO "Route to … stale"` / "Resetting next hop" lines). Use this to A/B M4
(attempts-to-delivery, total airtime).

### 3. Hardware via meshtastic MCP (auto-detect; 3+ devices for a real hop)

- `meshtastic-mcp/tests/mesh/test_nexthop_multihop_recovery.py` - **the multi-hop validator
  for this work** (added on this branch). Self-discovers an A - relay - C line, asserts a
  directed DM is delivered across the relay (next_hop + M1/M2/M3 engaged), and asserts
  delivery recovers after the relay is power-cycled (M3). Skips unless the bench is a true
  multi-hop line (≥3 roles via `--hub-profile`, endpoints out of direct RF range).
- `meshtastic-mcp/tests/mesh/test_direct_with_ack.py` - happy-path regression: a fresh/unique
  route still delivers a want_ack DM on the first/second try (M4's gate must keep this
  green).
- `meshtastic-mcp/tests/mesh/test_peer_offline_recovery.py` - 2-device recovery validator: peer
  off mid-conversation then back. Must stay green and ideally recover in fewer attempts.

### 4. Build / format sanity

native-macos **and** Docker both ways; trunk clang-format@16.0.3; a release `pio run` to
confirm the `#ifdef PIO_UNIT_TESTING` visibility widening does **not** leak into
production; sanity-check RAM headroom on the smallest nRF52 build for the ~384 B table.

---

## Verification status (as built on `nexthop-redux`)

| Tier                             | What ran                                                                            | Result              |
| -------------------------------- | ----------------------------------------------------------------------------------- | ------------------- |
| Unit (native-macos)              | `test_nexthop_routing` (31 cases)                                                   | ✅ 31/31            |
| Unit (Docker / Linux, CI parity) | `test_nexthop_routing`                                                              | ✅ 31/31            |
| Regression                       | `test_packet_history`, `test_hop_scaling`, `test_mqtt`, `test_traffic_management`   | ✅ 105/105          |
| Build                            | `pio run -e native-macos` (M4 off) and with `-DNEXTHOP_EARLY_FLOOD_ON_UNVERIFIED=1` | ✅ both link        |
| Format                           | trunk `clang-format@16.0.3`                                                         | ✅ no issues        |
| Simulator (CI `simulator-tests`) | `meshtasticd -s` + `meshtastic.test.testSimulator()` on native-macos                | ✅ exit 0, no crash |

**Pending (environment-blocked, not yet run):**

- **Multi-hop A-B-C recovery sim** - the `simulator/` broker hub is **not git-tracked**
  (only stale local `.pyc`), and two `meshtasticd -s` instances can't hear each other
  without it. The intermediate-node failure-count path and the M4 A/B therefore have unit
  coverage of their logic but no end-to-end multi-node run yet.
- **Hardware / multi-hop tier** - a committable bench test now exists:
  `meshtastic-mcp/tests/mesh/test_nexthop_multihop_recovery.py`. It self-discovers a real
  multi-hop pair (A - relay - C), asserts a directed DM is delivered across the relay, and
  asserts delivery recovers after the relay is power-cycled (the M3 path). It
  `pytest.skip`s cleanly unless the bench is a true line with endpoints out of direct RF
  range (≥3 roles via `--hub-profile`), so it's safe to commit and only asserts when the
  NextHop path is genuinely exercised. Collected + verified to skip without hardware;
  not yet run on a bench. `test_direct_with_ack.py` / `test_peer_offline_recovery.py`
  remain the 2-device happy-path/recovery regressions.

---

## Risks & limitations

- **Site-1 impostor rebroadcast** is unfixable without a wider field - documented; M1/M2
  only shrink its frequency.
- **Dense meshes flood DMs more often** - intended (a flooded DM arrives; a mis-unicast one
  black-holes). Call out in the PR so reviewers expect a slightly higher DM flood rate on
  very dense meshes.
- **M4 airtime** if the gate is too loose → default conservative + compile-gated +
  simulator A/B before broad enable.
- **RAM** ~384 B (32 slots); 16 slots (~192 B) with graceful LRU degradation if tight.
- **Asymmetric flap** not fully closed (a _new_ bad hop resets the counter); the TTL
  backstop bounds it. Per-hop failure history is future work (more RAM).

---

## How to continue this work (commit sequencing)

Each step is independently testable; land them as separate commits.

1. **M1 resolver + unit tests** - `NodeDB` only; no behavior change until wired. Lands the
   `resolveLastByte`/`resolveUniqueLastByte` primitive and its full unit-test matrix.
2. **M2 + wiring + tests** - `getNextHop` strict gate, learning gate, favorite-router
   preservation rewrite. Adds the `getNextHop` and site-4 tests.
3. **M3 health table + decay + tests** - RAM `RouteHealth` table, decay-on-read, failure/
   success accounting, reconciliation with the existing last-retry reset. Adds the
   route-health unit tests and the simulator recovery check.
4. **M4 gated tuning** - early-flood-on-unverified behind the compile flag; simulator A/B
   and hardware regression.

Reference plan (with the same content) was developed at
`~/.claude/plans/nexthop-routing-for-direct-lexical-shell.md` on the author's machine; this
in-repo doc is the canonical handoff copy.
