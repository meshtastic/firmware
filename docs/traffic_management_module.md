# The Traffic Management Module (TMM)

TMM is an optional module that shapes **transit** traffic on busy meshes. Large networks get
noisy fast - repeated position packets, bursty senders, and unknown/undecryptable frames all
burn limited airtime and power - and TMM filters or answers that traffic before it is
rebroadcast. On supported targets it **ships enabled** (`has_traffic_management` defaults to
true) with position dedup running at its 11 h default; the other features each default off, so
the module is on out of the box but opt-in per feature. It was introduced in
[meshtastic/firmware#9358](https://github.com/meshtastic/firmware/pull/9358).

This document covers the module's behaviour, with a deep dive on the two TMM-specific
NodeInfo features - **direct-serve** (answering NodeInfo requests on another node's behalf)
and the **throttling** that bounds it. The identity/traffic-state stores those features read
from are documented separately in [node_info_stores.md](node_info_stores.md); this file owns
the direct-serve and throttle behaviour, that file owns the stores.

Sources of truth: `src/modules/TrafficManagementModule.{h,cpp}`, defaults in
`src/mesh/Default.h`.

---

## How it runs

- **Enablement is three-gated.** Compile-time `HAS_TRAFFIC_MANAGEMENT` (with the
  `MESHTASTIC_EXCLUDE_TRAFFIC_MANAGEMENT` build exclusion), then the runtime
  `moduleConfig.has_traffic_management` presence flag. While the runtime gate is off, the
  packet path, the maintenance sweep, the NodeDB write-through hooks, and the cache accessors
  all no-op - content, maintenance, and reads are keyed to the same condition.
- **It runs before `RoutingModule`** in `callModules()`. Returning `STOP` from
  `handleReceived()` fully consumes a packet, so it is never rebroadcast; `CONTINUE` lets it
  proceed through normal relay handling.
- **State is cheap.** Per-node traffic-shaping counters live in a flat 10-byte
  `UnifiedCacheEntry` array (position fingerprint, rate/unknown counters, modular tick
  stamps, a next-hop hint, and a 4-bit role fallback) - see
  [node_info_stores.md §3](node_info_stores.md). Direct-serve additionally reads the PSRAM
  NodeInfo payload cache (or the NodeDB fallback when that cache is absent).

## What it does

| Feature                  | Default        | In one line                                                    |
| ------------------------ | -------------- | -------------------------------------------------------------- |
| Position dedup           | on, 11 h       | Suppresses a stationary sender's repeated position broadcasts. |
| Per-sender rate limit    | off            | Caps how many transit packets one sender may spend per window. |
| Unknown-packet filter    | off            | Drops a sender's undecryptable traffic past a threshold.       |
| NodeInfo direct response | off            | Answers a NodeInfo request on the target's behalf (see below). |
| Position precision clamp | channel-driven | Truncates relayed position to the channel's precision.         |

Config lives under `moduleConfig.traffic_management`; the per-feature sections below give the
exact fields, defaults, and behaviour. NodeInfo direct response has its own deep-dive sections
after these.

### Position dedup

`position_min_interval_secs` (default 11 h; `0` disables). Drops a duplicate position from the
same sender inside the interval, where "duplicate" means the same fingerprint on the channel's
`position_precision` grid (firmware default 19-bit, ~90 m cells). Role caps only ever _shorten_
the interval: **tracker / TAK tracker → 1 h**, **lost-and-found → 15 min**.

### Per-sender rate limit

`rate_limit_window_secs` + `rate_limit_max_packets` (default off; either `0` disables). Drops a
sender's transit packets once it exceeds the budget within the window.

### Unknown-packet filter

`unknown_packet_threshold` (default `0` = off). Drops undecryptable traffic from a sender once it
passes the threshold within a ~5 min window.

### NodeInfo direct response

`nodeinfo_direct_response_max_hops` (default `0` = off). When set, a neighbour that already
holds the target's identity answers a unicast NodeInfo request on its behalf, saving the full
round trip. This is TMM's most security-sensitive feature; the serve gates and the throttle
that bounds it are covered in the two dedicated sections below.

### Position precision clamp

Driven by the channel's `position_precision` ceiling (else the 19-bit firmware default).
`alterReceived()` truncates relayed position coordinates to that precision.

### Shelved

Present in the config surface but currently no-ops in the module, deferred until the right
heuristics are settled: hop exhaustion for position/telemetry (`exhaust_hop_position` /
`exhaust_hop_telemetry`) and `router_preserve_hops`. `alterReceived()` leaves rebroadcast hop
handling untouched.

---

## NodeInfo direct response (direct-serve)

Normally a unicast NodeInfo request travels all the way to the target and the reply travels
all the way back. On a large mesh that is several hops of airtime per lookup. When
`nodeinfo_direct_response_max_hops > 0`, a neighbour that already holds the target's identity
answers **on the target's behalf** with a spoofed reply, cutting the round trip to one hop.

**Data source.** The reply payload comes from the TMM NodeInfo payload cache (PSRAM-backed;
full cached `User` plus provenance metadata) or, on builds without that cache, from the
NodeDB fallback. Both are described in [node_info_stores.md §4](node_info_stores.md); this
feature is a _consumer_ of them.

**Decision pipeline** (`shouldRespondToNodeInfo()`), in order - any failure returns `false`
and the request is left to propagate normally:

1. **Eligibility** (checked by the caller): `nodeinfo_direct_response_max_hops > 0`,
   `NODEINFO_APP` portnum, `want_response`, and the packet is unicast, not to us, not from us.
2. **Hop clamp** (`isMinHopsFromRequestor()`): respond only when the requester is within the
   role-clamped hop ceiling - **routers up to 3 hops** (`kRouterDefaultMaxHops`, may be
   lowered by config), **clients direct-only, 0 hops** (`kClientDefaultMaxHops`).
3. **Identity lookup**: NodeInfo cache hit (cache path) or NodeDB fallback (fallback path).
4. **Staleness gate (6 h)**: never vouch for a node not genuinely _heard_ within the serve
   window. Only a real observed frame stamps the recency bit - seeding and write-through are
   knowledge, not observation, so a silent node can never look alive to this path.
5. **Key-provenance gate** (`TMM_NODEINFO_REPLAY_SIGNED_GATE`, default on): vouch only for
   an identity whose key is proven - XEdDSA-verified (directly or inherited from NodeDB) **or**
   manually verified out-of-band. Both paths honour both channels: the cache path via
   `keyProven()`, the NodeDB fallback path via `HAS_XEDDSA_SIGNED | IS_KEY_MANUALLY_VERIFIED`. A
   trust-on-first-use identity is left for the genuine node - or another cache-holder that _has_
   proof - to answer. Bypassed when PKI is compiled out.
6. **Throttle** (`directResponseAllowed()`): see the next section.

**The spoofed reply.** On success TMM emits a NodeInfo reply with `from` set to the _target_
(so the requester sees a valid answer), `to` the requester, `hop_limit = 0` (one hop only),
`request_id` the original packet id, and the OK_TO_MQTT bit set from local
`config.lora.config_ok_to_mqtt` policy. The requester's own identity claim in the request is
**not** written back to NodeDB - a unicast NodeInfo is unsigned, so treating it as an
identity update would be unauthenticated. `nodeinfo_cache_hits` counts only replies actually
sent.

---

## Throttling direct responses

A direct reply is addressed to the requesting packet's `from` and spoofs the requested
target - and **both fields are unauthenticated header data**. Without a bound, an attacker
crafts requests carrying a victim's address as `from`, and every neighbour holding the target
transmits at the victim: a reflector-amplification primitive. The throttle is the security
core of this feature, checked immediately before a reply would go out so requests declined for
other reasons never consume the budget.

**Three bounds**, all keyed off `clockMs()` and evaluated under `cacheLock`:

| Bound                                            | Window | Bounds                                           |
| ------------------------------------------------ | ------ | ------------------------------------------------ |
| Per requester (`kDirectResponsePerRequesterMs`)  | 60 s   | how much any single node can be made to receive  |
| Per target (`kDirectResponsePerTargetMs`)        | 60 s   | how often we vouch for the same identity         |
| Global airtime floor (`kDirectResponseGlobalMs`) | 1 s    | total spoofed TX, regardless of key distribution |

**Mechanism.** The two per-key bounds are fixed **8-slot LRU tables in internal RAM**
(`directRequesterSeen`, `directTargetSeen`) - _not_ the PSRAM NodeInfo cache - so they behave
identically with and without PSRAM, on the cache path and the NodeDB-fallback path alike.
Timestamps are full `uint32` milliseconds compared by wrap-safe subtraction, so there is no
tick clock and no maintenance sweep to keep them honest. `directResponseAllowed(requester,
target, now)` resolves a slot in _both_ tables before stamping either - so a reply one axis
throttles never consumes the other axis's budget - then records the send on all three bounds.
The global floor is a single stamp, checked first as the cheap common case.

**When a table fills.** For an unseen key with no free slot, `directResponseSlot()` evicts the
**least-recently-used** entry (smallest last-reply time) and admits the new key. The LRU
victim is by construction the entry closest to expiring anyway, so eviction is the
lowest-cost choice. An attacker who cycles more than 8 distinct requesters or targets - easy,
since both are unauthenticated - evicts entries and defeats _per-key_ throttling for the
cycled keys; that is expected, and why the **global 1 s floor is the hard backstop**. It is a
single stamp, cannot fill, and caps total spoofed replies at ~1/s no matter what. Per-key
throttling degrades gracefully to the floor under pressure.

**Throttled is not dropped.** A throttled request returns `false`, which lets
`handleReceived()` `CONTINUE`: the request forwards toward the genuine target (which can
answer itself) rather than being black-holed. A requester whose first reply was lost on a
noisy link would otherwise get silence for the whole window; repeats of the same packet id
are already absorbed by the router's duplicate detection.

**Evolution.** The original design split throttling by path: a per-entry `respTick` stamp in
each NodeInfo cache slot (cache path, 30 s, swept for wrap-safety) plus a single module-global
stamp for the NodeDB fallback (30 s, neither per-requester nor per-target). Those two routes
were unified into the symmetric per-requester + per-target RAM tables above, aligned to a
single 60 s window, so both axes hold with and without PSRAM and the cache entry no longer
carries throttle state.

---

## Configuration

All tunables live under `moduleConfig.traffic_management`; the whole module is gated by the
`has_traffic_management` presence flag, and each per-feature section above lists its own
field(s) and default. Two related sets of knobs are **firmware constants, not config**: the
role-based position caps `default_traffic_mgmt_tracker_position_min_interval_secs` (1 h) and
`default_traffic_mgmt_lost_and_found_position_min_interval_secs` (15 min), and the direct-serve
throttle windows (the `kDirectResponse*Ms` constants).

## See also

- [node_info_stores.md](node_info_stores.md) - the NodeDB hot store, warm tier, TMM NodeInfo
  payload cache, and unified cache that the direct-serve path reads from, plus their trust,
  provenance, and anti-entropy model.
