# SphereOfInfluenceModule: Influential Factors & Definitions

This document defines the key data signals and derived factors that influence hop recommendations and mesh classification.

---

## Observation Windows (Time Buckets)

Four rolling time windows, each providing a different temporal lens on mesh activity:

| Window  | Duration | Data                   | Purpose                                                                                                        |
| ------- | -------- | ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| **1h**  | 3600 s   | `nodesPerHop[WIN_1H]`  | Current active nodes; immediate mesh density for EVENT/DENSE classification                                    |
| **2h**  | 7200 s   | `nodesPerHop[WIN_2H]`  | Standard "online" baseline (mirrors `NUM_ONLINE_SECS`); primary for MEGAMESH/LARGE/SMALL/NORMAL classification |
| **6h**  | 21600 s  | `nodesPerHop[WIN_6H]`  | Stable presence; sleeping/intermittent nodes; detects EXTENDED topology (long chains)                          |
| **12h** | 43200 s  | `nodesPerHop[WIN_12H]` | Mesh persistence; identifies genuine members vs transient visitors; detects long-term structural changes       |

Each window is populated independently in a single NodeDB scan: per node, determine which windows it falls within and increment all applicable buckets.

**Per-window data structure:**

```cpp
uint16_t nodesPerHop[4][HOP_MAX + 1]   // 4 windows × 8 hops = 32 uint16_t = 64 bytes
uint16_t totalActive[4]                // 4 uint16_t = 8 bytes
uint16_t unknownHopCount[4]            // nodes with valid age but no has_hops_away
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

### Sampling case: NodeDB near capacity

When `totalActive[window] > 0.9 × MAX_NODEDB`:

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

### Three politeness regimes

**Regime 1: GENEROUS (factor = 2.0) — mesh is quiet**

- Triggered when: `totalActive[WIN_6H] / totalActive[WIN_1H] > 2.0`
- Scenario: Mesh has 100 nodes but only 30 in the last hour (many sleeping/intermittent)
- Interpretation: 1h histogram severely underrepresents true mesh size
- Action: Be willing to extend hops to reach nodes that exist but aren't currently transmitting
- Example: allow hop extension if adds up to 100% more nodes (2.0×)

**Regime 2: DEFAULT (factor = 1.5) — mesh is stable**

- Triggered when: `0.5 ≤ totalActive[WIN_6H] / totalActive[WIN_1H] ≤ 2.0`
- Scenario: Mesh composition is consistent across the 6h observation
- Interpretation: 1h count is reliable and representative
- Action: Moderate extension allowance
- Example: allow hop extension if adds up to 50% more nodes (1.5×)

**Regime 3: STRICT (factor = 1.25) — rapid influx / mesh growing**

- Triggered when: `totalActive[WIN_6H] / totalActive[WIN_1H] < 0.5`
- Scenario: Mesh has grown significantly since 6h ago (many new arrivals in last 2h)
- Interpretation: 1h histogram is the ground truth; older observations are stale
- Action: Conservative extension; trust current observations
- Example: allow hop extension only if adds up to 25% more nodes (1.25×)

### Implementation

```cpp
float stabilityRatio = getMeshSizeEstimate(WIN_6H) / max(getMeshSizeEstimate(WIN_1H), 1.0f);

float politenessFactor;
if (stabilityRatio > SOI_STABILITY_GENEROUS_RATIO) {        // > 2.0
  politenessFactor = SOI_POLITENESS_GENEROUS;               // 2.0
} else if (stabilityRatio < SOI_STABILITY_STRICT_RATIO) {   // < 0.5
  politenessFactor = SOI_POLITENESS_STRICT;                 // 1.25
} else {
  politenessFactor = SOI_POLITENESS_DEFAULT;              // 1.5
}
```

### Constants

```cpp
#define SOI_POLITENESS_DEFAULT       1.5f
#define SOI_POLITENESS_GENEROUS      2.0f
#define SOI_POLITENESS_STRICT        1.25f
#define SOI_STABILITY_GENEROUS_RATIO 2.0f    // 6h/1h > 2.0
#define SOI_STABILITY_STRICT_RATIO   0.5f    // 6h/1h < 0.5
#define SOI_SAMPLING_DENOMINATOR     5       // 1-in-5 packet sampling
```

---

## How they work together

1. **Rebuild histograms** (every 60 s) — scan NodeDB, populate all 4 windows
2. **Estimate mesh size** — check if NodeDB is full; extrapolate if needed
3. **Compute stability** — `ratio = 6h_size / 1h_size`
4. **Select politeness** — ratio determines factor (generous/threshold/strict)
5. **Find base hop** — scan histogram cumulatively to reach ~40 nodes
6. **Apply politeness** — check if hop+1 is polite; extend if acceptable
7. **Apply role bonus** — TRACKER adds +2, SENSOR adds +1, etc.
8. **Cap at user config** — never exceed `hop_limit + 2`

---

## Example scenarios

### Scenario A: Small stable mesh (10 nodes, all hop 0)

- 1h: [10, 0, 0...], 2h: [10, 0, 0...], 6h: [10, 0, 0...], 12h: [10, 0, 0...]
- Stability ratio: 10/10 = 1.0 → DEFAULT regime
- Base hop: hop 0 (10 ≥ 40? no, so base_hop = HOP_MAX = 7)
- Polite extend? Never reaches 40, so no
- Final: hop 7 (no restriction; too small to constrain)
- CLIENT result: 7 (respects user cap if lower)

### Scenario B: 50-node normal mesh

- 1h: [15, 20, 10, 5, 0...], total 50
- 6h: [18, 22, 10, 4, 1, 0...], total 55
- Stability ratio: 55/50 = 1.1 → DEFAULT regime (1.5×)
- Base hop: hop 2 (cumulative = 45 ≥ 40)
- Polite extend to hop 3? nodesPerHop[3] = 5 → 45+5=50; 50 ≤ 45×1.5=67.5? yes
- Base hop becomes 3
- CLIENT result: min(3, user_cap)
- TRACKER result: min(5, user_cap+2)
- SENSOR result: min(5, user_cap+1)

### Scenario C: 200-node dense event (influx)

- 1h: [120, 60, 15, 5, 0...], total 200
- 6h: [60, 40, 20, 10, 5, 5, 5, 5], total ~150 (older nodes dropping out)
- Stability ratio: 150/200 = 0.75 → DEFAULT regime (1.5×)
  - Note: even though 6h < 1h, still in the 0.5–2.0 range
- Base hop: hop 0 (cumulative = 120 ≥ 40)
- Polite extend to hop 1? nodesPerHop[1] = 60 → 120+60=180; 180 ≤ 120×1.5=180? yes, exactly equal
- Base hop becomes 1
- CLIENT result: 1 (compressed hop scope due to density)

### Scenario D: Long chain (EXTENDED)

- 1h: [5, 5, 5, 5, 5, 5, 5, 5], total 40
- 6h: [5, 5, 5, 5, 5, 5, 6, 6], total 42
- Stability ratio: 42/40 = 1.05 → DEFAULT (1.5×)
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
