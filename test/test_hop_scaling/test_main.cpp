#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#if HAS_VARIABLE_HOPS

#include "FSCommon.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include "modules/HopScalingModule.h"
#include <cstdio>
#include <cstring>
#include <memory>

// Unity only shows TEST_MESSAGE output. printf goes to stdout which the runner swallows.
#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                          \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

static constexpr NodeNum kLocalNode = 0x11111111;

// Shared mock clock — drives CompactHistogram::nowMs() and HopScalingModule's rollSampleWindow()
static uint32_t &mockTime = CompactHistogram::s_testNowMs;
static constexpr uint32_t ONE_HOUR_MS = 3600UL * 1000UL;

// ---------------------------------------------------------------------------
// MockNodeDB — lets tests inject nodes with controlled hop and age values
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        numMeshNodes = 0;
    }

    void addTestNode(NodeNum num, uint8_t hopsAway, bool hasHops, uint32_t ageSecs, bool viaMqtt = false)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.has_hops_away = hasHops;
        node.hops_away = hopsAway;
        node.via_mqtt = viaMqtt;
        node.last_heard = getTime() - ageSecs;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

// ---------------------------------------------------------------------------
// Test shim — expose protected methods for direct invocation
// ---------------------------------------------------------------------------
class HopScalingTestShim : public HopScalingModule
{
  public:
    using HopScalingModule::recordEviction;
    using HopScalingModule::recordPacketSender;
    using HopScalingModule::runOnce;
    using HopScalingModule::samplePacketForHistogram;

    using HopScalingModule::getEvictionsCurrentHour;
    using HopScalingModule::getLastActivityWeight;
    using HopScalingModule::getLastRequiredHop;
    using HopScalingModule::getLastScaleFactor;
    using HopScalingModule::getRollingEvictionAverage;

    // Test-only clock and window helpers (require UNIT_TEST friend access)
    void setSampleWindowStartMs(uint32_t ms) { sampleWindowStartMs = ms; }
    void rollSampleWindowTest(bool earlyTrigger) { rollSampleWindow(earlyTrigger); }
    void rollHourTest() { rollHour(); }
    void setHistogramDenominator(uint8_t d) { hopScalingHistogram.setSamplingDenominator(d); }

    // Size introspection for test_memory_layout
    static constexpr size_t sizeofSelf() { return sizeof(HopScalingModule); }
    static constexpr size_t sizeofHistogram() { return sizeof(CompactHistogram); }
    static constexpr size_t sizeofSampleTracker() { return sizeof(SampleTracker); }
    static constexpr size_t sizeofHopBucket() { return sizeof(HopBucket); }
    static constexpr size_t sizeofSnapshot() { return sizeof(Snapshot); }
};

static MockNodeDB *mockNodeDB = nullptr;

// Create deterministic IDs whose low bits are well distributed across modulo/bitmask buckets.
// This avoids aliasing to a single residue class and better exercises both samplers:
// - HopScaling legacy sampler: nodeId % denominator == 0
// - CompactHistogram sampler:   (nodeId & (denominator - 1)) == 0
static uint32_t makeDistributedNodeId(uint32_t baseId, uint32_t ordinal, uint32_t salt = 0)
{
    return baseId + salt + (ordinal * 33u);
}

// ---------------------------------------------------------------------------
// Helpers — mesh topology builders
// ---------------------------------------------------------------------------

// Helper: add N nodes at a given hop with ages spread across a time range.
// ageSecs is the base age; nodes get ageSecs + i*stride seconds old.
static void addNodesAtHop(uint32_t baseId, uint8_t hop, uint32_t count, uint32_t ageSecs, uint32_t stride = 10)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t nodeId = makeDistributedNodeId(baseId, i, static_cast<uint32_t>(hop) << 8);
        mockNodeDB->addTestNode(nodeId, hop, true, ageSecs + i * stride);
    }
}

// Feed sampled traffic that produces a rollingSampledAvg12h commensurate with meshSize.
// Uses the mock clock to advance time by one hour per roll so windowFraction=1.0.
// hopDist[] gives the number of nodes at each hop (indexed 0..HOP_MAX); nodes are
// injected per-hop-bucket so the histogram receives a realistic hop distribution.
static void injectSampleTraffic(HopScalingTestShim &shim, uint32_t baseId, uint16_t meshSize, const uint16_t hopDist[HOP_MAX + 1],
                                uint8_t numRolls = 16)
{
    HopScalingModule::resetSamplingDenominator();
    shim.setHistogramDenominator(CompactHistogram::DENOM_MIN);

    for (uint8_t roll = 0; roll < numRolls; ++roll) {
        // Advance mock clock by one hour so the sample window covers a full hour
        mockTime += ONE_HOUR_MS;

        uint16_t ordinal = 0;
        for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
            for (uint16_t n = 0; n < hopDist[hop]; ++n) {
                // No per-roll salt: same node IDs appear each hour, modelling a stable mesh
                // topology where the same neighbours are heard repeatedly.
                const uint32_t nodeId = makeDistributedNodeId(baseId, ordinal);
                shim.recordPacketSender(nodeId);
                shim.samplePacketForHistogram(nodeId, hop);
                ++ordinal;
            }
        }
        shim.rollSampleWindowTest(false);
        shim.rollHourTest();
    }
}

static void assertCompactHistogramActive(HopScalingTestShim &shim)
{
    TEST_ASSERT_GREATER_THAN_UINT8(0, shim.getCompactHistogramEntryCount());
    TEST_ASSERT_TRUE(shim.getCompactHistogramAllSampleCount() > 0);
}

// Scenario A: Dense local mesh — 110 nodes with a heavy core at hops 0–2.
// A telemetry broadcast here shouldn't need to travel far.
// Expected: hop 1 or 2 (cumulative easily exceeds 40 by hop 1-2).
static void buildDenseLocalMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 25, 120);  // 25 nodes at hop 0, ~2 min old
    addNodesAtHop(0x2000, 1, 30, 300);  // 30 nodes at hop 1, ~5 min old
    addNodesAtHop(0x3000, 2, 15, 600);  // 15 nodes at hop 2, ~10 min old
    addNodesAtHop(0x4000, 3, 5, 1200);  //  5 nodes at hop 3, ~20 min old
    addNodesAtHop(0x5000, 4, 10, 1800); //  10 nodes at hop 4, ~30 min old
    addNodesAtHop(0x6000, 5, 15, 2400); //  15 nodes at hop 5, ~40 min old
    addNodesAtHop(0x7000, 6, 10, 3000); //  10 nodes at hop 6, ~50 min old
    // Total: 110 nodes. Cumulative at hop 1: 25+30=55 > 40 → expect baseHop=1.
    // Politeness extension uses the 40-node floor: limit = 40*polite.
    // Strict: 40*1.25=50, Default: 40*1.5=60, Generous: 40*2=80; extending to hop 2 gives 70.
    // So only generous extends to hop 2.
}

// Scenario B: Spread sparse mesh — nodes distributed across hops 0–7.
// A position broadcast needs moderate reach.
// Expected: hop 3 or 4 (need to accumulate across several hops to reach 40).
static void buildSpreadSparseMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 5, 120);   //  5 at hop 0
    addNodesAtHop(0x2000, 1, 8, 300);   //  8 at hop 1
    addNodesAtHop(0x3000, 2, 12, 600);  // 12 at hop 2
    addNodesAtHop(0x4000, 3, 15, 900);  // 15 at hop 3
    addNodesAtHop(0x5000, 4, 10, 1200); // 10 at hop 4
    addNodesAtHop(0x6000, 5, 6, 1800);  //  6 at hop 5
    addNodesAtHop(0x7000, 6, 10, 3000); // 10 at hop 6
    addNodesAtHop(0x8000, 7, 10, 3600); // 10 at hop 7
    // Total: 76 nodes. Cumulative: h0=5, h1=13, h2=25, h3=40 → baseHop=3.
    // Politeness check: extending to hop 4 → 50; limit = 40*polite.
    // Generous: 40*2=80, 50<80 → extend. Default: 40*1.5=60, 50<60 → extend. Strict: 40*1.25=50, 50<=50 → extend.
}

// Scenario C: Deep linear chain — few nodes per hop, spread to hop 7.
// Remote/rural network where nodes are spread thinly over many hops.
// Expected: hop 7 (can't reach 40 nodes even at max hop).
static void buildDeepLinearChain()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 2, 120);  // 2 at hop 0
    addNodesAtHop(0x2000, 1, 3, 300);  // 3 at hop 1
    addNodesAtHop(0x3000, 2, 3, 600);  // 3 at hop 2
    addNodesAtHop(0x4000, 3, 4, 900);  // 4 at hop 3
    addNodesAtHop(0x5000, 4, 3, 1200); // 3 at hop 4
    addNodesAtHop(0x6000, 5, 2, 1800); // 2 at hop 5
    addNodesAtHop(0x7000, 6, 2, 2400); // 2 at hop 6
    addNodesAtHop(0x8000, 7, 3, 3600); // 3 at hop 7
    // Total: 22 nodes. Never reaches 40 → baseHop = HOP_MAX = 7.
}

// Scenario D: Concentrated hop-2 cluster — simulates a router-heavy setup
// where most nodes are exactly 2 hops away (common with hilltop routers).
// Expected: hop 2 (cumulative at hop 2 exceeds 40).
static void buildRouterCluster()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 3, 120);  //  3 at hop 0 (local)
    addNodesAtHop(0x2000, 1, 5, 300);  //  5 at hop 1 (routers)
    addNodesAtHop(0x3000, 2, 45, 600); // 45 at hop 2 (behind routers)
    addNodesAtHop(0x4000, 3, 8, 1200); //  8 at hop 3
    addNodesAtHop(0x5000, 4, 3, 1200); // 3 at hop 4
    addNodesAtHop(0x6000, 5, 2, 1800); // 2 at hop 5
    addNodesAtHop(0x7000, 6, 2, 2400); // 2 at hop 6
    addNodesAtHop(0x8000, 7, 3, 3600); // 3 at hop 7
    // Total: 71. Cumulative: h0=3, h1=8, h2=53 > 40 → baseHop=2.
    // Politeness uses the 40-node floor: extending to hop 3 gives 61.
    // Strict: 40*1.25=50, Default: 40*1.5=60, Generous: 40*2=80.
    // So only generous extends to hop 3.
}

// Scenario E: Megamesh with high eviction turnover.
// DB is near capacity (199 of 200 nodes) with steady evictions.
// Scale factor should inflate counts, potentially reducing required hops.
static void buildMegamesh()
{
    mockNodeDB->clearTestNodes();
    // Fill DB near capacity with nodes spread across hops 0-7
    addNodesAtHop(0x01000, 0, 30, 120);  // 30 at hop 0
    addNodesAtHop(0x02000, 1, 40, 300);  // 40 at hop 1
    addNodesAtHop(0x03000, 2, 35, 600);  // 35 at hop 2
    addNodesAtHop(0x04000, 3, 30, 900);  // 30 at hop 3
    addNodesAtHop(0x05000, 4, 20, 1200); // 20 at hop 4
    addNodesAtHop(0x06000, 5, 15, 1800); // 15 at hop 5
    addNodesAtHop(0x07000, 6, 14, 2400); // 14 at hop 6
    addNodesAtHop(0x08000, 7, 15, 3600); // 15 at hop 7
    // Total: 199 nodes (99.5% of 200). At capacity.
    // Without scaling: cumulative at h0=30, h1=70 > 40 → hop 1. With scaling, even more compressed.
}

// ---------------------------------------------------------------------------
// Tests — Topology-driven hop reduction scenarios
// ---------------------------------------------------------------------------

// Scenario A: Dense local mesh — telemetry broadcast.
// 110 nodes with a heavy local core and a thinner tail to hop 6.
// Cumulative at hop 1 = 55 > 40, so a telemetry packet should stay near hop 1-2.
void test_dense_local_telemetry()
{
    TEST_MESSAGE("=== Dense local mesh: telemetry broadcast ===");
    TEST_MESSAGE("Topology: 110 nodes with 25/30/15 nodes at hops 0/1/2 and a thinner tail to hop 6.");
    TEST_MESSAGE("Expectation: cumulative reaches 55 nodes by hop 1, and only generous politeness extends to hop 2.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDenseLocalMesh();
    TEST_MESSAGE("Injecting sampled traffic to seed sampledEst.");
    const uint16_t distA[HOP_MAX + 1] = {25, 30, 15, 5, 10, 15, 10, 0};
    injectSampleTraffic(*shim, 0x91000000, 110, distA);

    shim->runOnce();

    TEST_MSG_FMT("Dense local: hop=%u scale=%.2f actWt=%.2f polite=%.2f mode=%u", shim->getLastRequiredHop(),
                 (double)shim->getLastScaleFactor(), (double)shim->getLastActivityWeight(), 0.0,
                 0); // polite/mode logged by module

    TEST_MESSAGE("Assertion: 55 nodes are reachable by hop 1, so the result should remain tightly constrained.");
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 1);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario B: Spread sparse mesh — position broadcast.
// 76 nodes distributed across hops 0-7. Need hop 3 to reach 40.
// A position packet should travel about 3-4 hops.
void test_spread_sparse_position()
{
    TEST_MESSAGE("=== Spread sparse mesh: position broadcast ===");
    TEST_MESSAGE("Topology: 76 nodes spread across all hops, reaching 40 nodes only when hop 3 is included.");
    TEST_MESSAGE("Expectation: base hop lands at 3 and politeness can extend reach to hop 4.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSparseMesh();
    TEST_MESSAGE("Injecting sampled traffic to seed sampledEst.");
    const uint16_t distB[HOP_MAX + 1] = {5, 8, 12, 15, 10, 6, 10, 10};
    injectSampleTraffic(*shim, 0x92000000, 76, distB);

    shim->runOnce();

    TEST_MSG_FMT("Spread sparse: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    TEST_MESSAGE("Assertion: hop should settle in the 3-5 range for this spread-out mesh.");
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 5);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario C: Deep linear chain — position broadcast.
// 22 nodes spread thinly across hops 0-7. Never reaches 40 cumulative.
// Must use maximum hops to reach everyone.
void test_deep_chain_position()
{
    TEST_MESSAGE("=== Deep linear chain: position broadcast ===");
    TEST_MESSAGE("Topology: 22 nodes spread thinly across hops 0-7, never reaching the 40-node floor.");
    TEST_MESSAGE("Expectation: the module must keep HOP_MAX so end-of-chain nodes remain reachable.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDeepLinearChain();
    TEST_MESSAGE("Injecting sampled traffic to seed sampledEst.");
    const uint16_t distC[HOP_MAX + 1] = {2, 3, 3, 4, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x93000000, 22, distC);

    shim->runOnce();

    TEST_MSG_FMT("Deep chain: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    TEST_MESSAGE("Assertion: total active population is too small to reduce hop count.");
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, shim->getLastRequiredHop());
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario D: Router cluster — telemetry broadcast.
// 71 nodes, 45 of which sit behind routers at hop 2. Cumulative at hop 2 = 53.
// A telemetry packet generally needs 2-3 hops here.
void test_router_cluster_telemetry()
{
    TEST_MESSAGE("=== Router cluster: telemetry broadcast ===");
    TEST_MESSAGE("Topology: 71 nodes with a concentrated 45-node cluster at hop 2 behind a small router layer.");
    TEST_MESSAGE("Expectation: cumulative reaches 53 at hop 2; only generous politeness extends to hop 3.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRouterCluster();
    TEST_MESSAGE("Injecting sampled traffic to seed sampledEst.");
    const uint16_t distD[HOP_MAX + 1] = {3, 5, 45, 8, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x94000000, 71, distD);

    shim->runOnce();

    TEST_MSG_FMT("Router cluster: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    TEST_MESSAGE("Assertion: result should stay in the 2-4 range for this router-backed topology.");
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 2);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 4);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario E: Megamesh with evictions — telemetry broadcast.
// The NodeDB holds 199 nodes (near capacity) but the true mesh is ~2000 nodes;
// the DB is a small window of a much larger network constantly churning.
// Sustained evictions grow the scale factor; sampledEst (via packet sampling) reports ~2000.
// Hop limit should compress as the full estimated mesh density is recognized.
void test_megamesh_eviction_scaling()
{
    TEST_MESSAGE("=== Megamesh with eviction scaling ===");
    TEST_MESSAGE("Topology: NodeDB at capacity (199 nodes) representing a ~2000-node mesh with sustained eviction pressure.");
    TEST_MESSAGE("Expectation: sampledEst ~2000, scale factor grows above 1.0, required hop compresses toward the local core.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildMegamesh();

    // Pre-warm the sampling estimate with 16 simulated hours of ~2000-node traffic.
    // Hop distribution proportional to the 199-node DB topology.
    const uint16_t distE[HOP_MAX + 1] = {301, 402, 352, 301, 201, 151, 141, 151};
    injectSampleTraffic(*shim, 0x9B000000, 2000, distE);

    TEST_MESSAGE("Phase 1: initial run without evictions.");
    shim->runOnce();
    uint8_t hopBefore = shim->getLastRequiredHop();

    TEST_MSG_FMT("Megamesh initial: hop=%u scale=%.2f evict/h=%.1f", hopBefore, (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    TEST_MESSAGE("Phase 2: simulate 3 hours of increasing evictions plus sampled-node traffic.");
    for (int hour = 0; hour < 3; hour++) {
        for (int i = 0; i < 15 * (hour + 1); i++)
            shim->recordEviction();

        // Feed sampled nodes: advance mock clock one hour and inject 2000 IDs
        // with hop distribution matching the megamesh topology.
        mockTime += ONE_HOUR_MS;
        {
            // Megamesh hop distribution scaled up to ~2000 nodes (proportional to 199-node DB)
            const uint16_t megaDist[HOP_MAX + 1] = {301, 402, 352, 301, 201, 151, 141, 151};
            uint16_t ordinal = 0;
            for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
                for (uint16_t n = 0; n < megaDist[hop]; ++n) {
                    const uint32_t nodeId = makeDistributedNodeId(0x9C000000u, ordinal, static_cast<uint32_t>(hour) * 0x10000u);
                    shim->recordPacketSender(nodeId);
                    shim->samplePacketForHistogram(nodeId, hop);
                    ++ordinal;
                }
            }
        }
        shim->rollSampleWindowTest(false);

        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("Megamesh hour %d: hop=%u scale=%.2f evict/h=%.1f", hour + 1, shim->getLastRequiredHop(),
                     (double)shim->getLastScaleFactor(), (double)shim->getRollingEvictionAverage());
    }

    TEST_MESSAGE("Assertion: sustained evictions should push scale above 1.0 and keep hop well below HOP_MAX.");
    TEST_ASSERT_TRUE(shim->getLastScaleFactor() > 1.0f);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario F: Transition from sparse to dense — watches hop decrease.
// Start with a deep chain (22 nodes, hop=7), then add nodes at low hops
// to simulate a mesh growing denser locally. Hop should decrease.
void test_sparse_to_dense_transition()
{
    TEST_MESSAGE("=== Sparse-to-dense transition ===");
    TEST_MESSAGE("Topology change: start with a 22-node deep chain, then inject 50 new neighbors at hops 0-1.");
    TEST_MESSAGE("Expectation: hop should drop sharply once the local neighborhood becomes dense.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    TEST_MESSAGE("Phase 1: sparse chain should hold at HOP_MAX.");
    buildDeepLinearChain();
    TEST_MESSAGE("Injecting sampled traffic to seed sampledEst.");
    const uint16_t distC2[HOP_MAX + 1] = {2, 3, 3, 4, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x95000000, 22, distC2);
    shim->runOnce();
    uint8_t hopSparse = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 1 sparse: hop=%u (expect %u)", hopSparse, HOP_MAX);
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, hopSparse);

    TEST_MESSAGE("Phase 2: add 50 nodes at hops 0-1 and drive the hourly recomputation path.");
    addNodesAtHop(0xA000, 0, 25, 120);
    addNodesAtHop(0xB000, 1, 25, 300);

    // The histogram is now the primary hop decision-maker, so the 50 new neighbours must also
    // be fed into it — exactly mirroring what the firmware does when it hears them on-air.
    // Node IDs must match what addNodesAtHop() computed (same makeDistributedNodeId call).
    TEST_MESSAGE("Feeding 50 new Phase 2 nodes into the histogram (simulating on-air reception).");
    for (uint32_t i = 0; i < 25; ++i)
        shim->samplePacketForHistogram(makeDistributedNodeId(0xA000, i, static_cast<uint32_t>(0) << 8), 0);
    for (uint32_t i = 0; i < 25; ++i)
        shim->samplePacketForHistogram(makeDistributedNodeId(0xB000, i, static_cast<uint32_t>(1) << 8), 1);

    TEST_MESSAGE("Running six more cycles to force the hourly recomputation.");
    for (int run = 0; run < 6; run++)
        shim->runOnce();

    uint8_t hopDense = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 2 dense: hop=%u (expect <= 3)", hopDense);

    TEST_MESSAGE("Assertion: added local nodes should reduce required hop below the sparse baseline.");
    TEST_ASSERT_TRUE(hopDense < hopSparse);
    TEST_ASSERT_TRUE(hopDense <= 3);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Scenario G: State persistence across restarts.
// Run a megamesh for one hour, destroy instance, create a new one.
// The restored rolling averages should be non-zero.
void test_state_persistence()
{
    TEST_MESSAGE("=== State persistence across restart ===");
    TEST_MESSAGE("Expectation: eviction-derived state should survive instance teardown and reload.");

    TEST_MESSAGE("Phase 1: accumulate eviction history and save state.");
    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();
        buildMegamesh();

        for (int i = 0; i < 10; i++)
            shim->recordEviction();

        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("Phase 1: evict/h=%.1f scale=%.2f", (double)shim->getRollingEvictionAverage(),
                     (double)shim->getLastScaleFactor());

        TEST_ASSERT_TRUE(shim->getRollingEvictionAverage() > 0.0f);
        hopScalingModule = nullptr;
    }

    TEST_MESSAGE("Phase 2: create a new module instance and verify restored rolling averages.");
    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();
        buildMegamesh();

        shim->runOnce();

        TEST_MSG_FMT("Phase 2 restored: evict/h=%.1f scale=%.2f hop=%u", (double)shim->getRollingEvictionAverage(),
                     (double)shim->getLastScaleFactor(), shim->getLastRequiredHop());

        TEST_ASSERT_TRUE(shim->getRollingEvictionAverage() > 0.0f);
        hopScalingModule = nullptr;
    }
}

// Scenario H: Early sample flush under load.
// A near-capacity mesh with 96+ unique sampled IDs triggers rollSampleWindow(true).
void test_early_sample_flush()
{
    TEST_MESSAGE("=== Early sample flush under load ===");
    TEST_MESSAGE("Expectation: feeding more than 96 unique sampled IDs triggers an early sample-window roll.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildMegamesh();

    // Ensure histogram captures all test samples
    HopScalingModule::setDenominatorForTest(1);
    shim->setHistogramDenominator(1);

    shim->runOnce();

    TEST_MESSAGE("Feeding 120 unique sampled node IDs (96 needed for flush)");
    for (uint32_t i = 1; i <= 120; i++) {
        const uint32_t nodeId = makeDistributedNodeId(0x96000000, i, 0x55u);
        shim->recordPacketSender(nodeId);
        shim->samplePacketForHistogram(nodeId, static_cast<uint8_t>(i % (HOP_MAX + 1)));
    }

    TEST_MESSAGE("Running one more cycle after the forced flush to emit updated status.");
    shim->runOnce();

    TEST_MSG_FMT("After early flush: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// ---------------------------------------------------------------------------
// Tests — Hourly roll, intermediate status, startup lifecycle
// ---------------------------------------------------------------------------

// Hourly roll cycle: evictions + sampled nodes integrated into rolling averages.
void test_hourly_roll()
{
    TEST_MESSAGE("=== Hourly roll cycle ===");
    TEST_MESSAGE("Expectation: hourly integration folds evictions and sampled senders into rolling averages.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSparseMesh();

    // Ensure histogram captures all test samples
    HopScalingModule::setDenominatorForTest(1);
    shim->setHistogramDenominator(1);

    for (int i = 0; i < 12; i++)
        shim->recordEviction();

    for (uint32_t i = 1; i <= 30; i++) {
        const uint32_t nodeId = makeDistributedNodeId(0x97000000, i, 0xAAu);
        shim->recordPacketSender(nodeId);
        shim->samplePacketForHistogram(nodeId, static_cast<uint8_t>(i % (HOP_MAX + 1)));
    }

    // Run 13 cycles (instead of 7) to reach multiple hourly rolls and stable histogram state
    for (int run = 0; run < 13; run++) {
        int32_t interval = shim->runOnce();
        TEST_ASSERT_GREATER_THAN(0, interval);
    }

    TEST_MSG_FMT("Hourly roll: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    TEST_ASSERT_TRUE(shim->getRollingEvictionAverage() > 0.0f);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

// Intermediate status: between hourly updates, hop/scale should not change.
void test_intermediate_status()
{
    TEST_MESSAGE("=== Intermediate status (no recomputation) ===");
    TEST_MESSAGE("Expectation: runs between hourly updates should leave hop and scale unchanged.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRouterCluster();

    shim->runOnce();
    uint8_t hopAfterInitial = shim->getLastRequiredHop();
    float scaleAfterInitial = shim->getLastScaleFactor();

    TEST_MSG_FMT("Initial: hop=%u scale=%.2f", hopAfterInitial, (double)scaleAfterInitial);

    for (int run = 0; run < 3; run++) {
        shim->runOnce();
        TEST_ASSERT_EQUAL_UINT8(hopAfterInitial, shim->getLastRequiredHop());
        TEST_ASSERT_FLOAT_WITHIN(0.001f, scaleAfterInitial, shim->getLastScaleFactor());
    }

    TEST_MSG_FMT("After 3 intermediate runs: hop=%u scale=%.2f (unchanged)", shim->getLastRequiredHop(),
                 (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

// Startup with blank state: no persisted file, fresh defaults.
void test_startup_blank_state()
{
    TEST_MESSAGE("=== Startup with blank state ===");
    TEST_MESSAGE("Expectation: a fresh instance starts with zeroed rolling averages and a valid hop result.");

#ifdef FSCom
    TEST_MESSAGE("Clearing persisted hop scaling state files before startup test.");
    FSCom.remove("/prefs/hop_scaling.bin");
    FSCom.remove("/prefs/soi.bin");
#endif

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDeepLinearChain();

    int32_t interval = shim->runOnce();

    TEST_ASSERT_GREATER_THAN(0, interval);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= HOP_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, shim->getRollingEvictionAverage());

    TEST_MSG_FMT("Startup blank: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    hopScalingModule = nullptr;
}

// Final scenario summary emitted into the test log for quick CI review.
void test_scenario_summary_output()
{
    TEST_MESSAGE("=== Scenario summary ===");
    TEST_MESSAGE("Scenario       | Nodes  | Distribution                 | Scale | Hop    | Why");
    TEST_MESSAGE("A: Dense local | 110    | 25/30/15/5/10/15/10 h0-6     | 1.0   | 1-2    | 55 nodes at h1 >> 40");
    TEST_MESSAGE("B: Spread      | 76     | 5/8/12/15/10/6/10/10 h0-7    | 1.0   | 3-4    | Need h3 to reach 40");
    TEST_MESSAGE("C: Deep chain  | 22     | 2/3/3/4/3/2/2/3 h0-7         | 1.0   | 7      | Never reaches 40");
    TEST_MESSAGE("D: Router      | 71     | 3/5/45/8/3/2/2/3 h0-7        | 1.0   | 2-3    | 45-node hop-2 cluster");
    TEST_MESSAGE("E: Megamesh    | 199    | 30/40/35/30/20/15/14/15 h0-7 | >1.0  | 0-1    | Scale inflates counts");
    TEST_MESSAGE("F: Transition  | 22->72 | Chain -> dense local         | 1.0   | 7-><=3 | Adapts to new neighbors");
    TEST_MESSAGE("G: Persistence | --     | --                           | --    | --     | State survives reboot");
    TEST_MESSAGE("H: Early flush | 199    | Near-capacity sampled mesh   | --    | --     | Sample tracker overflow");
}

// Print exact sizeof() for every major type in the hop-scaling subsystem.
// Always passes — fails only if the build breaks, which the compile step catches first.
static void test_memory_layout()
{
    TEST_MSG_FMT("%-35s  %6s  %s", "Type", "bytes", "Notes");
    TEST_MSG_FMT("%-35s  %6zu  %s", "Record", sizeof(Record), "nodeHash:16 + hops:3 + seen:13 (32-bit packed)");
    TEST_MSG_FMT("%-35s  %6zu  %s", "CompactHistogram::PerHopCounts", sizeof(CompactHistogram::PerHopCounts),
                 "perHop[8](16) + total(2) + pad");
    TEST_MSG_FMT("%-35s  %6zu  %s", "CompactHistogram", HopScalingTestShim::sizeofHistogram(), "entries[128](512) + state");
    TEST_MSG_FMT("%-35s  %6zu  %s", "SampleTracker", HopScalingTestShim::sizeofSampleTracker(),
                 "slots[128](512) + uniqueCount(2) + pad");
    TEST_MSG_FMT("%-35s  %6zu  %s", "HopBucket", HopScalingTestShim::sizeofHopBucket(),
                 "perHop[8](16) + unknownHop(2) + total(2)");
    TEST_MSG_FMT("%-35s  %6zu  %s", "Snapshot", HopScalingTestShim::sizeofSnapshot(), "4x HopBucket");
    TEST_MSG_FMT("%-35s  %6zu  %s", "HopScalingModule (instance)", HopScalingTestShim::sizeofSelf(),
                 "everything above + base class overhead");

    const size_t histogram = HopScalingTestShim::sizeofHistogram();
    const size_t tracker = HopScalingTestShim::sizeofSampleTracker();
    const size_t total = HopScalingTestShim::sizeofSelf();
    const size_t other = total - histogram - tracker;
    TEST_MSG_FMT("  histogram: %zu B  tracker: %zu B  other: %zu B  total: %zu B", histogram, tracker, other, total);
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Unity setup / teardown / main
// ---------------------------------------------------------------------------

void setUp(void)
{
    if (!mockNodeDB)
        mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();

    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    myNodeInfo.my_node_num = kLocalNode;
    nodeDB = mockNodeDB;

    // Clear persisted state files so each test starts with a blank rolling average.
    // Without this, saveState() from a prior test (e.g. megamesh) leaks sampledEst
    // and rollingAvgRollCount into all subsequent tests via loadState() in the constructor.
#ifdef FSCom
    FSCom.remove("/prefs/hop_scaling.bin");
    FSCom.remove("/prefs/soi.bin");
#endif

    // Reset mock clock to a known base (1 hour in so subtraction never underflows)
    mockTime = ONE_HOUR_MS;

    // Disable denominator jitter so SAMPLING_DENOMINATOR stays on deterministic values
    // and injectSampleTraffic() produces consistent results across runs.
    HopScalingModule::setSamplingJitter(false);
    // Reset denominator to its initial value (8) so each test starts from a known state.
    // Adaptive adjustment is left enabled — the denominator self-tunes during
    // injectSampleTraffic() rolls and settles to a value commensurate with mesh size.
    HopScalingModule::resetSamplingDenominator();
}

void tearDown(void)
{
    hopScalingModule = nullptr;
    HopScalingModule::setSamplingJitter(true); // restore default for any non-test code paths
}

void setup()
{
    initializeTestEnvironment();
    nodeDB = mockNodeDB;

    UNITY_BEGIN();
    // Topology-driven hop reduction
    RUN_TEST(test_dense_local_telemetry);
    RUN_TEST(test_spread_sparse_position);
    RUN_TEST(test_deep_chain_position);
    RUN_TEST(test_router_cluster_telemetry);
    RUN_TEST(test_megamesh_eviction_scaling);
    RUN_TEST(test_sparse_to_dense_transition);
    // Storage and lifecycle
    RUN_TEST(test_state_persistence);
    RUN_TEST(test_early_sample_flush);
    RUN_TEST(test_hourly_roll);
    RUN_TEST(test_intermediate_status);
    RUN_TEST(test_startup_blank_state);
    RUN_TEST(test_scenario_summary_output);
    RUN_TEST(test_memory_layout);
    exit(UNITY_END());
}

void loop() {}

#else // !HAS_VARIABLE_HOPS

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
