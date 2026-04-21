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

// Shared mock clock — drives HopScalingModule::nowMs()
static uint32_t &mockTime = HopScalingModule::s_testNowMs;
static constexpr uint32_t ONE_HOUR_MS = 3600UL * 1000UL;

// ---------------------------------------------------------------------------
// MockNodeDB — not used for hop decisions any more, kept for completeness
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
// Test shim — expose protected/private members for direct invocation
// ---------------------------------------------------------------------------
class HopScalingTestShim : public HopScalingModule
{
  public:
    using HopScalingModule::runOnce;
    using HopScalingModule::samplePacketForHistogram;

    using HopScalingModule::getLastRequiredHop;

    // Test-only helpers (require UNIT_TEST friend access)
    void rollHourTest() { rollHour(); }
    void setHistogramDenominator(uint8_t d) { setSamplingDenominator(d); }

    // Size introspection for test_memory_layout
    static constexpr size_t sizeofSelf() { return sizeof(HopScalingModule); }
};

static MockNodeDB *mockNodeDB = nullptr;

// Create deterministic IDs whose low bits are well distributed across the hash filter.
// HopScalingModule sampler: (nodeId & (denominator - 1)) == 0
static uint32_t makeDistributedNodeId(uint32_t baseId, uint32_t ordinal, uint32_t salt = 0)
{
    return baseId + salt + (ordinal * 33u);
}

// ---------------------------------------------------------------------------
// Helpers — mesh topology builders
// ---------------------------------------------------------------------------

// Helper: add N nodes at a given hop with ages spread across a time range.
static void addNodesAtHop(uint32_t baseId, uint8_t hop, uint32_t count, uint32_t ageSecs, uint32_t stride = 10)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t nodeId = makeDistributedNodeId(baseId, i, static_cast<uint32_t>(hop) << 8);
        mockNodeDB->addTestNode(nodeId, hop, true, ageSecs + i * stride);
    }
}

// Feed sampled traffic into the histogram.
// Advances mock clock by one hour per roll and calls rollHour() so each roll produces data.
static void injectSampleTraffic(HopScalingTestShim &shim, uint32_t baseId, const uint16_t hopDist[HOP_MAX + 1],
                                uint8_t numRolls = 16)
{
    shim.setHistogramDenominator(HopScalingModule::DENOM_MIN);

    for (uint8_t roll = 0; roll < numRolls; ++roll) {
        mockTime += ONE_HOUR_MS;

        uint16_t ordinal = 0;
        for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
            for (uint16_t n = 0; n < hopDist[hop]; ++n) {
                const uint32_t nodeId = makeDistributedNodeId(baseId, ordinal);
                shim.samplePacketForHistogram(nodeId, hop);
                ++ordinal;
            }
        }
        shim.rollHourTest();
    }
}

static void assertCompactHistogramActive(HopScalingTestShim &shim)
{
    TEST_ASSERT_GREATER_THAN_UINT8(0, shim.getCompactHistogramEntryCount());
    TEST_ASSERT_TRUE(shim.getCompactHistogramAllSampleCount() > 0);
}

// ---------------------------------------------------------------------------
// Topology builders
// ---------------------------------------------------------------------------

// Scenario A: Dense local mesh — 110 nodes, heavy at hops 0–2.
static void buildDenseLocalMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 25, 120);
    addNodesAtHop(0x2000, 1, 30, 300);
    addNodesAtHop(0x3000, 2, 15, 600);
    addNodesAtHop(0x4000, 3, 5, 1200);
    addNodesAtHop(0x5000, 4, 10, 1800);
    addNodesAtHop(0x6000, 5, 15, 2400);
    addNodesAtHop(0x7000, 6, 10, 3000);
}

// Scenario B: Spread sparse mesh — 76 nodes across hops 0–7.
static void buildSpreadSparseMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 5, 120);
    addNodesAtHop(0x2000, 1, 8, 300);
    addNodesAtHop(0x3000, 2, 12, 600);
    addNodesAtHop(0x4000, 3, 15, 900);
    addNodesAtHop(0x5000, 4, 10, 1200);
    addNodesAtHop(0x6000, 5, 6, 1800);
    addNodesAtHop(0x7000, 6, 10, 3000);
    addNodesAtHop(0x8000, 7, 10, 3600);
}

// Scenario C: Deep linear chain — 22 thin nodes, never reaches 40.
static void buildDeepLinearChain()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 2, 120);
    addNodesAtHop(0x2000, 1, 3, 300);
    addNodesAtHop(0x3000, 2, 3, 600);
    addNodesAtHop(0x4000, 3, 4, 900);
    addNodesAtHop(0x5000, 4, 3, 1200);
    addNodesAtHop(0x6000, 5, 2, 1800);
    addNodesAtHop(0x7000, 6, 2, 2400);
    addNodesAtHop(0x8000, 7, 3, 3600);
}

// Scenario D: Router cluster — 71 nodes, 45 at hop 2.
static void buildRouterCluster()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 3, 120);
    addNodesAtHop(0x2000, 1, 5, 300);
    addNodesAtHop(0x3000, 2, 45, 600);
    addNodesAtHop(0x4000, 3, 8, 1200);
    addNodesAtHop(0x5000, 4, 3, 1200);
    addNodesAtHop(0x6000, 5, 2, 1800);
    addNodesAtHop(0x7000, 6, 2, 2400);
    addNodesAtHop(0x8000, 7, 3, 3600);
}

// Scenario E: Megamesh — 199 nodes (DB near capacity).
static void buildMegamesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x01000, 0, 30, 120);
    addNodesAtHop(0x02000, 1, 40, 300);
    addNodesAtHop(0x03000, 2, 35, 600);
    addNodesAtHop(0x04000, 3, 30, 900);
    addNodesAtHop(0x05000, 4, 20, 1200);
    addNodesAtHop(0x06000, 5, 15, 1800);
    addNodesAtHop(0x07000, 6, 14, 2400);
    addNodesAtHop(0x08000, 7, 15, 3600);
}

// ---------------------------------------------------------------------------
// Tests — Topology-driven hop reduction scenarios
// ---------------------------------------------------------------------------

void test_dense_local_telemetry()
{
    TEST_MESSAGE("=== Dense local mesh: telemetry broadcast ===");
    TEST_MESSAGE("Topology: 110 nodes with 25/30/15 nodes at hops 0/1/2 and a thinner tail to hop 6.");
    TEST_MESSAGE("Expectation: cumulative reaches 55 nodes by hop 1, result stays tightly constrained.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDenseLocalMesh();
    const uint16_t distA[HOP_MAX + 1] = {25, 30, 15, 5, 10, 15, 10, 0};
    injectSampleTraffic(*shim, 0x91000000, distA);
    shim->runOnce();

    TEST_MSG_FMT("Dense local: hop=%u", shim->getLastRequiredHop());
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 1);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_spread_sparse_position()
{
    TEST_MESSAGE("=== Spread sparse mesh: position broadcast ===");
    TEST_MESSAGE("Topology: 76 nodes spread across all hops, reaching 40 nodes only when hop 3 is included.");
    TEST_MESSAGE("Expectation: hop settles in the 3-5 range.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSparseMesh();
    const uint16_t distB[HOP_MAX + 1] = {5, 8, 12, 15, 10, 6, 10, 10};
    injectSampleTraffic(*shim, 0x92000000, distB);
    shim->runOnce();

    TEST_MSG_FMT("Spread sparse: hop=%u", shim->getLastRequiredHop());
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 5);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_deep_chain_position()
{
    TEST_MESSAGE("=== Deep linear chain: position broadcast ===");
    TEST_MESSAGE("Topology: 22 nodes spread thinly across hops 0-7, never reaching the 40-node floor.");
    TEST_MESSAGE("Expectation: module must keep HOP_MAX.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDeepLinearChain();
    const uint16_t distC[HOP_MAX + 1] = {2, 3, 3, 4, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x93000000, distC);
    shim->runOnce();

    TEST_MSG_FMT("Deep chain: hop=%u", shim->getLastRequiredHop());
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, shim->getLastRequiredHop());
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_router_cluster_telemetry()
{
    TEST_MESSAGE("=== Router cluster: telemetry broadcast ===");
    TEST_MESSAGE("Topology: 71 nodes with a concentrated 45-node cluster at hop 2.");
    TEST_MESSAGE("Expectation: result stays in the 2-4 range.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRouterCluster();
    const uint16_t distD[HOP_MAX + 1] = {3, 5, 45, 8, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x94000000, distD);
    shim->runOnce();

    TEST_MSG_FMT("Router cluster: hop=%u", shim->getLastRequiredHop());
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 2);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 4);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_megamesh_eviction_scaling()
{
    TEST_MESSAGE("=== Megamesh with eviction scaling ===");
    TEST_MESSAGE("Topology: NodeDB at capacity (199 nodes), ~2000-node mesh with sustained eviction pressure.");
    TEST_MESSAGE("Expectation: sustained evictions tracked in rolling average, hop stays well below HOP_MAX.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildMegamesh();

    const uint16_t distE[HOP_MAX + 1] = {301, 402, 352, 301, 201, 151, 141, 151};
    injectSampleTraffic(*shim, 0x9B000000, distE);

    shim->runOnce();
    uint8_t hopBefore = shim->getLastRequiredHop();
    TEST_MSG_FMT("Megamesh initial: hop=%u", hopBefore);

    for (int hour = 0; hour < 3; hour++) {
        mockTime += ONE_HOUR_MS;
        {
            const uint16_t megaDist[HOP_MAX + 1] = {301, 402, 352, 301, 201, 151, 141, 151};
            uint16_t ordinal = 0;
            for (uint8_t hop = 0; hop <= HOP_MAX; ++hop) {
                for (uint16_t n = 0; n < megaDist[hop]; ++n) {
                    const uint32_t nodeId = makeDistributedNodeId(0x9C000000u, ordinal, static_cast<uint32_t>(hour) * 0x10000u);
                    shim->samplePacketForHistogram(nodeId, hop);
                    ++ordinal;
                }
            }
        }

        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("Megamesh hour %d: hop=%u", hour + 1, shim->getLastRequiredHop());
    }

    TEST_MESSAGE("Assertion: hop stays well below HOP_MAX on a large-distribution mesh.");
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_sparse_to_dense_transition()
{
    TEST_MESSAGE("=== Sparse-to-dense transition ===");
    TEST_MESSAGE("Topology change: start with a 22-node deep chain, then inject 50 new neighbors at hops 0-1.");
    TEST_MESSAGE("Expectation: hop drops sharply once the local neighborhood becomes dense.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    buildDeepLinearChain();
    const uint16_t distC2[HOP_MAX + 1] = {2, 3, 3, 4, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x95000000, distC2);
    shim->runOnce();
    uint8_t hopSparse = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 1 sparse: hop=%u (expect %u)", hopSparse, HOP_MAX);
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, hopSparse);

    addNodesAtHop(0xA000, 0, 25, 120);
    addNodesAtHop(0xB000, 1, 25, 300);

    for (uint32_t i = 0; i < 25; ++i)
        shim->samplePacketForHistogram(makeDistributedNodeId(0xA000, i, static_cast<uint32_t>(0) << 8), 0);
    for (uint32_t i = 0; i < 25; ++i)
        shim->samplePacketForHistogram(makeDistributedNodeId(0xB000, i, static_cast<uint32_t>(1) << 8), 1);

    for (int run = 0; run < 13; run++)
        shim->runOnce();

    uint8_t hopDense = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 2 dense: hop=%u (expect <= 3)", hopDense);

    TEST_ASSERT_TRUE(hopDense < hopSparse);
    TEST_ASSERT_TRUE(hopDense <= 3);
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_state_persistence()
{
    TEST_MESSAGE("=== State persistence across restart ===");
    TEST_MESSAGE("Expectation: histogram entries survive instance teardown and reload.");

    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();

        const uint16_t dist[HOP_MAX + 1] = {5, 8, 12, 10, 5, 3, 2, 1};
        injectSampleTraffic(*shim, 0x9D000000, dist, 2);

        TEST_MSG_FMT("Phase 1: entries=%u hop=%u", shim->getEntryCount(), shim->getLastRequiredHop());
        TEST_ASSERT_GREATER_THAN_UINT8(0, shim->getEntryCount());
        hopScalingModule = nullptr;
    }

    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();
        shim->runOnce();

        TEST_MSG_FMT("Phase 2 restored: entries=%u hop=%u", shim->getEntryCount(), shim->getLastRequiredHop());
        TEST_ASSERT_GREATER_THAN_UINT8(0, shim->getEntryCount());
        hopScalingModule = nullptr;
    }
}

void test_hourly_roll()
{
    TEST_MESSAGE("=== Hourly roll cycle ===");
    TEST_MESSAGE("Expectation: histogram accumulates data and provides valid hop recommendation after multiple rolls.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSparseMesh();
    shim->setHistogramDenominator(HopScalingModule::DENOM_MIN);

    for (uint32_t i = 1; i <= 30; i++) {
        const uint32_t nodeId = makeDistributedNodeId(0x97000000, i, 0xAAu);
        shim->samplePacketForHistogram(nodeId, static_cast<uint8_t>(i % (HOP_MAX + 1)));
    }

    for (int run = 0; run < 13; run++) {
        int32_t interval = shim->runOnce();
        TEST_ASSERT_GREATER_THAN(0, interval);
    }

    TEST_MSG_FMT("Hourly roll: hop=%u entries=%u", shim->getLastRequiredHop(), shim->getEntryCount());
    assertCompactHistogramActive(*shim);

    hopScalingModule = nullptr;
}

void test_intermediate_status()
{
    TEST_MESSAGE("=== Intermediate status (no recomputation) ===");
    TEST_MESSAGE("Expectation: runs between hourly updates leave hop unchanged.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRouterCluster();
    const uint16_t distD[HOP_MAX + 1] = {3, 5, 45, 8, 3, 2, 2, 3};
    injectSampleTraffic(*shim, 0x98000000, distD);

    shim->runOnce();
    uint8_t hopAfterInitial = shim->getLastRequiredHop();
    TEST_MSG_FMT("Initial: hop=%u", hopAfterInitial);

    for (int run = 0; run < 3; run++) {
        shim->runOnce();
        TEST_ASSERT_EQUAL_UINT8(hopAfterInitial, shim->getLastRequiredHop());
    }

    TEST_MSG_FMT("After 3 intermediate runs: hop=%u (unchanged)", shim->getLastRequiredHop());
    hopScalingModule = nullptr;
}

void test_startup_blank_state()
{
    TEST_MESSAGE("=== Startup with blank state ===");
    TEST_MESSAGE("Expectation: fresh instance starts with zeroed rolling averages and a valid hop result.");

#ifdef FSCom
    FSCom.remove("/prefs/hopScalingState.bin");
#endif

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDeepLinearChain();

    int32_t interval = shim->runOnce();

    TEST_ASSERT_GREATER_THAN(0, interval);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= HOP_MAX);

    TEST_MSG_FMT("Startup blank: hop=%u", shim->getLastRequiredHop());

    hopScalingModule = nullptr;
}

void test_scenario_summary_output()
{
    TEST_MESSAGE("=== Scenario summary ===");
    TEST_MESSAGE("Scenario       | Nodes  | Distribution                 | Hop    | Why");
    TEST_MESSAGE("A: Dense local | 110    | 25/30/15/5/10/15/10 h0-6     | 1-2    | 55 nodes at h1 >> 40");
    TEST_MESSAGE("B: Spread      | 76     | 5/8/12/15/10/6/10/10 h0-7    | 3-4    | Need h3 to reach 40");
    TEST_MESSAGE("C: Deep chain  | 22     | 2/3/3/4/3/2/2/3 h0-7         | 7      | Never reaches 40");
    TEST_MESSAGE("D: Router      | 71     | 3/5/45/8/3/2/2/3 h0-7        | 2-3    | 45-node hop-2 cluster");
    TEST_MESSAGE("E: Megamesh    | 199    | 30/40/35/30/20/15/14/15 h0-7 | 0-1    | Dense low-hop histogram");
    TEST_MESSAGE("F: Transition  | 22->72 | Chain -> dense local         | 7-><=3 | Adapts to new neighbors");
    TEST_MESSAGE("G: Persistence | --     | --                           | --     | Eviction avg survives reboot");
}

static void test_memory_layout()
{
    TEST_MSG_FMT("%-35s  %6s  %s", "Type", "bytes", "Notes");
    TEST_MSG_FMT("%-35s  %6zu  %s", "Record", sizeof(Record), "nodeHash:16 + hops:3 + seen:13 (32-bit packed)");
    TEST_MSG_FMT("%-35s  %6zu  %s", "HopScalingModule::PerHopCounts", sizeof(HopScalingModule::PerHopCounts),
                 "perHop[8](16) + total(2)");
    TEST_MSG_FMT("%-35s  %6zu  %s", "HopScalingModule (instance)", HopScalingTestShim::sizeofSelf(),
                 "entries[128](512) + denom state + cached results + OSThread overhead");

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

#ifdef FSCom
    FSCom.remove("/prefs/hopScalingState.bin");
#endif

    // Reset mock clock to a known base (1 hour in so subtraction never underflows)
    mockTime = ONE_HOUR_MS;
}

void tearDown(void)
{
    hopScalingModule = nullptr;
}

void setup()
{
    initializeTestEnvironment();
    nodeDB = mockNodeDB;

    UNITY_BEGIN();
    RUN_TEST(test_dense_local_telemetry);
    RUN_TEST(test_spread_sparse_position);
    RUN_TEST(test_deep_chain_position);
    RUN_TEST(test_router_cluster_telemetry);
    RUN_TEST(test_megamesh_eviction_scaling);
    RUN_TEST(test_sparse_to_dense_transition);
    RUN_TEST(test_state_persistence);
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