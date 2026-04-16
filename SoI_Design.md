# SphereOfInfluenceModule: Influential Factors & Definitions

This document defines the key data signals and derived factors that influence hop recommendations and mesh classification.

---

## Observation Windows (Count/Time Buckets)

Five rolling time windows, each providing a different temporal lens on mesh activity:

| Window          | Duration | Data                                |
| --------------- | -------- | ----------------------------------- |
| **1h_current**  | 3600 s   | `totalActive[WIN_1H]`               |
| **1h_active**   | 3600 s   | `nodesPerHop[WIN_1H_1H_OLD]`        |
| **2h_current**  | 7200 s   | `totalActive[WIN_2H]`               |
| **2h old**      | 3600 s   | `nodesPerHop[WIN_1H_2H_OLD]`        |
| **3h_current**  | 10800 s  | `totalActive[WIN_3H]`               |
| **3h old**      | 3600 s   | `nodesPerHop[WIN_1H_3H_OLD]`        |
| **12h_current** | 43200 s  | `nodesPerHop[WIN_12H]`              |
| **1h**          | 3600 s   | `nodeDbEvictions[WIN_1H]`           |
| **12h**         | 43200 s  | `nodeDbEvictions[ROLL_AVE_WIN_12H]` |
|                 |          |                                     |

Each **xh_current** window is populated independently in a single NodeDB scan: per node, determine which windows it falls within and increment all applicable buckets.
Each **xh_old** window is populated as a sequential bucket - the oldest is emptied, and then the newest is populated from the nodeDB scan.
nodeDbEvictions are totaled each hour, and then the 12 hour rolling average is updated at the end of each hour.

**Per-window data structure:**

```cpp
uint16_t nodesPerHop[5][HOP_MAX + 1]   // 5 windows × 8 hops = 40 uint16_t = 80 bytes
uint16_t totalActive[5]                // 5 uint16_t = 10 bytes
uint16_t unknownHopCount[5]            // nodes with valid age but no has_hops_away
```

---

## Mesh Size Estimates

Accurately counting the true mesh size is critical for hop recommendations. NodeDB capacity limits (80–256 nodes per device) means we can't always store every node.

### Ground truth case: NodeDB not full

When `totalActive[window] < 0.9 × MAX_NODEDB`:

```cpp
estimatedMeshSize[window] = totalActive[window]
```

NodeDB is capturing most of the mesh; no extrapolation needed.

### Scaling case: NodeDB at capacity, low turnover

When `nodeDbEvictions[ROLL_AVE_WIN_12H] < 0.5 × MAX_NODEDB`:

the number of nodes at each hopcount is scaled based on the eviction rate.

```cpp
// Extrapolate
estimatedMeshSize[window] = sampledUniqueNodes[window] * nodeDbEvictions[ROLL_AVE_WIN_12H];
```

### Sampling case: NodeDB at capacity

When `nodeDbEvictions[ROLL_AVE_WIN_12H] > 0.5 × MAX_NODEDB && totalActive[window] > 0.9 × MAX_NODEDB`:

**Sampling method:** Use modulo sampling on incoming packet stream.

```cpp
static constexpr uint8_t SOI_SAMPLING_DENOMINATOR = 5;  // Sample 1-in-5 packets

if (packet.from % SOI_SAMPLING_DENOMINATOR == 0) {
  // This packet's sender is in our sample
  sampledUniqueNodes[window]++;
}

// Extrapolate
estimatedMeshSize[window] = sampledUniqueNodes[window] * SOI_SAMPLING_DENOMINATOR;
```

**Why modulo sampling:**

- O(1) memory (only track ~16 sampled IDs per window, deterministic set)
- O(1) per-packet cost
- Deterministic: node ID 0, 5, 10, 15… always sampled; reproduced across reboots
- Easy to tune sampling rate

**Caveats:**

- If node IDs are non-uniformly distributed (clustered by hardware batch), estimate may skew
- Meshtastic uses uint32_t for node IDs; clustering risk is low
- Sampling rate = 1/5 introduces ~√5 ≈ 2.2× variance; acceptable for directional estimates
- Minimum sample size (small meshes): if sampled count is 0 or very small (< 2), fall back to pure NodeDB count

### Selection logic

to be checked

```cpp
uint16_t getMeshSizeEstimate(WindowIndex w) const {
  if (totalActive[w] <= 0.9 * MAX_NODEDB) {
    // NodeDB has capture the full mesh
    return totalActive[w];
  } else {
    // NodeDB is full; extrapolate from sample
    uint16_t estimate = sampledUniqueNodes[w] * SOI_SAMPLING_DENOMINATOR;
    // Safety: never estimate smaller than what's in NodeDB
    return max(estimate, totalActive[w]);
  }
}
```

---

## Politeness Factor

**Definition:** A multiplier applied when deciding whether to extend a hop recommendation by +1 hop.

The hop algorithm finds a base hop count that reaches ~40 nodes. The politeness check asks: "If we add one more hop, how many _more_ nodes would we reach? Is that acceptable?"

### Why politeness matters

Extending from hop 0 to hop 1 in a 40-node mesh might only add 2 more nodes (42 total, +5% airtime).
That's efficient.

But extending from hop 0 to hop 1 in a 200-node event might add 150 more nodes (190 total, +375% airtime).
That's wasteful — the politeness check prevents this cliff jump.

### Politeness formula

```cpp
if (base_hop < HOP_MAX) {
  cumulative_at_base = count of nodes at hops 0..base_hop
  cumulative_if_extend = cumulative_at_base + nodesPerHop[base_hop + 1]

  if (cumulative_if_extend <= cumulative_at_base * politenessFactor):
    base_hop += 1  // Extension is "polite"
}
```

### Activity weight (trend across 1h → 2h → 3h windows)

Because NodeDB stores only the most recent `last_heard` per node, the 1–2h and 2–3h buckets contain nodes whose **last** contact falls in that slice — not all nodes active during that period. A node heard at both 2.5h and 1.5h ago only appears in the 1–2h bucket. Each bucket therefore represents nodes that **went quiet** during that window.

| Quantity         | Formula                             | Meaning                                                                                                                                            |
| ---------------- | ----------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `activityWeight` | `nodes_1h_2h / max(1, nodes_2h_3h)` | Above 1: more nodes went quiet recently (mesh churning / busy period ending)<br>Below 1: departures happened earlier (mesh stabilising or growing) |

**Trend states:**

When activity is weighted at less than an hour, stricter limits on affected nodes are required.
When activity is spread across a 3 hour window, less care is needed to ensure that the affected nodes are limited.

---

### Three politeness regimes

**Regime 1: GENEROUS (factor = 2.0) — mesh is quiet/shrinking**

- Triggered when: `activityWeight < 0.9`
- Scenario: Mesh had 80 nodes 2–3h ago but only 30 in the last hour (nodes leaving or going silent)
- Interpretation: Recent activity severely underrepresents ambient density
- Action: Be willing to extend hops to maintain connectivity with sparse-but-present nodes
- Example: allow hop extension if adds up to 100% more nodes (2.0×)

**Regime 2: DEFAULT (factor = 1.5) — mesh is stable**

- Triggered when: `0.9 < activityWeight < 1.2`
- Scenario: Mesh composition is consistent across the 3h observation
- Interpretation: 1h count is reliable and representative; mesh broadcast frequency is typically STABLE
- Action: Moderate extension allowance
- Example: allow hop extension if adds up to 50% more nodes (1.5×)

**Regime 3: STRICT (factor = 1.25) — rapid influx / mesh growing**

- Triggered when: `1.2 < activityWeight`
- Scenario: Mesh is busy in last hour
- Interpretation: 1h histogram is ground truth; older observations are stale
- Action: Conservative extension; trust current observations
- Example: allow hop extension only if adds up to 25% more nodes (1.25×)

### Implementation

```cpp
to populate
```

### Constants

```cpp
#define SOI_POLITENESS_DEFAULT       1.5f
#define SOI_POLITENESS_GENEROUS      2.0f
#define SOI_POLITENESS_STRICT        1.25f
#define SOI_STABILITY_GENEROUS_RATIO 2.0f    // 3h/1h > 2.0: mesh quieting
#define SOI_STABILITY_STRICT_RATIO   0.5f    // 3h/1h < 0.5: rapid influx
#define SOI_SLOPE_ACCEL_THRESHOLD    0.3f    // acceleration threshold for slope refinement
#define SOI_EXTENDED_TAIL_THRESHOLD  1.5f    // 6h/1h ratio indicating long-tail population
#define SOI_SAMPLING_DENOMINATOR     5       // 1-in-5 packet sampling
```

---

## Finding base hopstart

The `nodesPerHop[WIN_12H]` is scaled either based on the current nodeDB, the eviction ratio or sampling regime.

`nodesPerHop[WIN_12H] * scaleFactor` is the best estimate of actual node population at each hop, with scaleFactor compensating for eviction undercount. This is compared to `(nodesPerHop[WIN_1H_1H_OLD] + nodesPerHop[WIN_1H_2H_OLD] + nodesPerHop[WIN_1H_3H_OLD]) / 3` — the average of three 1h buckets, reducing the 3h sum to a single-hour equivalent. The higher of the two signals is used as the anticipated node count at each hop.

The affected nodes are determined by counting the running total of nodes at each hopstart until ~40 nodes is reached, and then the extension by a further hop is determined by the generosity factor.

## How they work together

0. **Build histograms** at startup, scan NodeDB, populate all windows
1. **Rebuild histograms** (every 60 minutes) — move 1-3 hour buckets up, scan NodeDB, populate longer windows
2. **Estimate mesh size** — check if NodeDB is full; extrapolate if needed
3. **Compute activity** — ratio of 1h–2h bucket vs 2h–3h bucket (`activityWeight`)
4. **Select politeness** — primary ratio determines base factor (generous/default/strict); slope refines; 6h tail may relax STRICT → DEFAULT
5. **Find base hop** — scan histogram cumulatively to reach ~40 nodes
6. **Apply politeness** — check if hop+1 is polite; extend if acceptable
7. **Apply role bonus** — TRACKER adds +2, SENSOR adds +1, etc.
8. **Cap at user config** — never exceed `hop_limit + 2`

---

## Example scenarios

### Scenario A: Small stable mesh (10 nodes, all hop 0)

- 1h: [10, 0, 0...], 2h: [10, 0, 0...], 3h: [10, 0, 0...], 6h: [10, 0, 0...], 12h: [10, 0, 0...]
- Primary ratio: 10/10 = 1.0 → DEFAULT regime
- Slope: slopeRecent=10/10=1.0, slopeOlder=10/10=1.0, slopeAccel=0 → STABLE; no regime refinement
- 6h cross-check: extendedTailRatio=10/10=1.0 → no long tail; classification unchanged
- Base hop: hop 0 (10 ≥ 40? no, so base_hop = HOP_MAX = 7)
- Polite extend? Never reaches 40, so no
- Final: hop 7 (no restriction; too small to constrain)
- CLIENT result: 7 (respects user cap if lower)

### Scenario B: 50-node normal mesh

- 1h: [15, 20, 10, 5, 0...], total 50
- 2h: [14, 19, 10, 4, 0...], total ~47
- 3h: [13, 19, 10, 4, 0...], total ~46 (same nodes, consistently active)
- Primary ratio: 46/50 = 0.92 → DEFAULT regime (1.5×)
- Slope: slopeRecent=50/47≈1.06, slopeOlder=47/46≈1.02, slopeAccel≈+0.04 → STABLE; no refinement
- 6h cross-check: WIN_6H≈55 → extendedTailRatio=55/50=1.1 → small tail; no regime change
- Base hop: hop 2 (cumulative = 45 ≥ 40)
- Polite extend to hop 3? nodesPerHop[3] = 5 → 45+5=50; 50 ≤ 45×1.5=67.5? yes
- Base hop becomes 3
- CLIENT result: min(3, user_cap)
- TRACKER result: min(5, user_cap+2)
- SENSOR result: min(5, user_cap+1)

### Scenario C: 200-node dense event (influx)

- 1h: [120, 60, 15, 5, 0...], total 200 (event crowd packing in)
- 2h: [20, 15, 10, 5, 0...], total 50 (early arrivals from 1–2h ago)
- 3h: [10, 8, 5, 2, 0...], total 25 (background mesh 2–3h ago, pre-event)
- Primary ratio: 25/200 = 0.125 → STRICT (< 0.5)
- Slope: slopeRecent=200/50=4.0, slopeOlder=50/25=2.0, slopeAccel=+2.0 → SPIKE/GROWING; reinforces STRICT
- 6h cross-check: WIN_6H≈30 → extendedTailRatio=30/200=0.15 → no long tail; STRICT confirmed
- Base hop: hop 0 (cumulative = 120 ≥ 40)
- Polite extend to hop 1? nodesPerHop[1] = 60 → 120+60=180; 180 ≤ 120×1.25=150? no
- Base hop stays 0
- CLIENT result: 0 (compressed hop scope due to extreme density)

### Scenario D: Long chain (EXTENDED)

- 1h: [5, 5, 5, 5, 5, 5, 5, 5], total 40
- 2h: [5, 5, 5, 5, 5, 5, 5, 5], total 40
- 3h: [5, 5, 5, 5, 5, 5, 5, 5], total 40 (consistent presence across all windows)
- Primary ratio: 40/40 = 1.0 → DEFAULT (1.5×)
- Slope: slopeRecent=40/40=1.0, slopeOlder=40/40=1.0, slopeAccel=0 → STABLE; no refinement
- 6h cross-check: WIN_6H≈42 → extendedTailRatio≈1.05 → no meaningful tail; classification unchanged
- Base hop: hop 7 (cumulative at each hop: 5, 10, 15, 20, 25, 30, 35, 40 — first ≥ 40 is at hop 7)
- Polite extend to hop 8? No hop 8 exists (max is 7)
- Base hop stays 7
- CLIENT result: 7 (full reach needed for chain health monitoring)

---

## Edge cases & notes

- **Unknown hop nodes** — counted in `unknownHopCount[window]` but not included in histogram. Don't affect hop recommendation (we can't determine distance for them).
- **MQTT nodes** — excluded from all buckets (use local-RF-only mesh observation).
- **Sampling with small N** — if sampled count < 2, don't extrapolate; fall back to NodeDB count.
- **Ratio compute safety** — always use `max(denominator, 1.0)` to avoid division by zero.

---

## NodeDB Eviction and Window Representativeness

NodeDB has a fixed capacity that varies by platform. When full, new nodes trigger **eviction** of the oldest existing node. This creates differential bias across time windows that affects how representative each window's counts are.

### Capacity limits

| Platform               | `MAX_NUM_NODES` | Notes                       |
| ---------------------- | --------------- | --------------------------- |
| STM32WL                | 10              | Extremely constrained       |
| nRF52840/nRF52833      | 80              | Moderate; common bottleneck |
| ESP32 (4 MB flash)     | 100             | Default                     |
| ESP32-S3 (8 MB flash)  | 200             | Expanded                    |
| ESP32-S3 (16 MB flash) | 250             | Large; rarely saturated     |

`isFull()` is also true if `getFreeHeap() < 1500 bytes`, which can trigger earlier than the node count limit on memory-constrained builds.

### Eviction priority (from `getOrCreateMeshNode`)

When the DB is full and a new node is seen:

1. **Scan all entries except index 0** (own node is never a candidate).
2. **Boring first**: find the oldest-`last_heard` node that is `!is_favorite && !is_ignored && public_key.size == 0`.
3. **Fallback**: if no boring node exists, find the oldest-`last_heard` node that is `!is_favorite && !is_ignored && !is_manually_key_verified`.
4. **Protected nodes** (`is_favorite`, `is_ignored`, manually-verified key) are **never evicted**.

The evicted node is the one with the **oldest `last_heard` timestamp** among eligible candidates. Nodes that transmitted recently are always the last to be removed.

### Impact on window accuracy

Because longer windows hold nodes with older `last_heard` values, they are disproportionately exposed to eviction when the DB is under pressure:

| Window      | Time bucket | Eviction risk                                                   | Accuracy at capacity                                             |
| ----------- | ----------- | --------------------------------------------------------------- | ---------------------------------------------------------------- |
| **WIN_1H**  | 0–1h ago    | Very low — most recent timestamps always survive                | High; reliable on all platforms                                  |
| **WIN_2H**  | 1–2h ago    | Low — evicted only in very high-turnover conditions             | Good; reliable on ESP32+                                         |
| **WIN_3H**  | 2–3h ago    | Moderate — at risk on nRF52 (80 nodes) or fast-turnover meshes  | Moderate; treat as lower bound on nRF52                          |
| **WIN_6H**  | 3–6h ago    | High — oldest eligible nodes; first evicted as new nodes arrive | Low on constrained platforms; use as indicative lower bound only |
| **WIN_12H** | 6–12h ago   | Very high — virtually always evicted when DB is full            | Unreliable at capacity; structural reference only                |

**Bias direction:** Eviction exclusively **undercounts** longer windows — evicted nodes simply disappear from the NodeDB scan. Counts are never inflated by eviction.

**Exception — protected node inflation:** Favorites and manually-verified nodes are never evicted, so they persist across all windows regardless of silence. A favorite node that has not transmitted in 5h still contributes to WIN_6H and WIN_12H. This provides a small upward bias to longer windows, partially counteracting the eviction undercount.

### Implications for slope and regime logic

**Why WIN_3H is preferred over WIN_6H as the primary stability reference:**

- On nRF52 (80 nodes), a mesh of 80–100 active nodes begins steady-state eviction; WIN_6H systematically drops nodes that went quiet after being heard
- WIN_3H is affected only when turnover is very aggressive (multiple full DB cycles within 3h), which is rare
- The 3h-vs-1h primary ratio retains directional reliability in all practical scenarios; the 6h ratio does not

**Slope signals (WIN_1H, WIN_2H, WIN_3H) are the most robust** because all three represent very recent data where eviction pressure is minimal. On any platform capable of seeing a meaningful mesh, these counts can be trusted to reflect actual activity.

**Effect of eviction on slope distortion:**

- If WIN_3H is undercounted due to eviction, `slopeOlder` (`WIN_2H / WIN_3H`) appears artificially large
- This would make a genuinely stable mesh look like a GROWING trend (slopeAccel > 0)
- Result: a potential false STRICT nudge on small platforms under sustained load
- Mitigation: the `SOI_SLOPE_ACCEL_THRESHOLD` of 0.3 provides some buffer; this distortion only materialises when the DB has been at capacity continuously for 3+ hours

**Effect on 6h cross-check:**

- On nRF52, `extendedTailRatio` (`WIN_6H / WIN_1H`) is unreliable when the DB has been at capacity; WIN_6H undercount makes the tail appear smaller than it is
- This means the STRICT → DEFAULT relaxation from the 6h cross-check may not fire when it should on constrained platforms
- This is a conservative failure mode: STRICT is held when STRICT might not be necessary; acceptable since STRICT is the safer direction

### Hop count accuracy

NodeDB stores only the **most recently observed** `hops_away` per node. When a node is placed into WIN_3H (heard 2–3h ago), the histogram uses the node's current stored hop count, not the one observed at the time it was last in range. If the node moved in the interim, the hop count is stale for that window. This is generally acceptable:

- Nodes in a fixed mesh rarely change hop count significantly over a few hours
- Mobile nodes (trackers) do move, but their recent activity drives WIN_1H which is authoritative
- Unknown hop count nodes (`unknownHopCount`) are still separated out and excluded from the histogram

**PKI node over-representation in WIN_3H/WIN_6H:**
Boring nodes (no public key) are evicted _before_ PKI-enabled nodes. In a full-DB scenario, older PKI nodes survive longer than older boring nodes. This means the hop distribution in longer windows will be slightly biased toward PKI-capable nodes; boring short-range devices are under-represented. The practical effect on hop recommendations is small but noted.
