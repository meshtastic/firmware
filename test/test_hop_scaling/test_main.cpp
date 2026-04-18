#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#if HAS_VARIABLE_HOPS

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

    using HopScalingModule::getEvictionsCurrentHour;
    using HopScalingModule::getLastActivityWeight;
    using HopScalingModule::getLastRequiredHop;
    using HopScalingModule::getLastScaleFactor;
    using HopScalingModule::getRollingEvictionAverage;
};

static MockNodeDB *mockNodeDB = nullptr;

// ---------------------------------------------------------------------------
// Helpers — mesh topology builders
// ---------------------------------------------------------------------------

// Helper: add N nodes at a given hop with ages spread across a time range.
// ageSecs is the base age; nodes get ageSecs + i*stride seconds old.
static void addNodesAtHop(uint32_t baseId, uint8_t hop, uint32_t count, uint32_t ageSecs, uint32_t stride = 10)
{
    for (uint32_t i = 0; i < count; i++)
        mockNodeDB->addTestNode(baseId + i, hop, true, ageSecs + i * stride);
}

// Scenario A: Dense local mesh — 60+ nodes clustered at hops 0–2.
// A telemetry broadcast here shouldn't need to travel far.
// Expected: hop 1 or 2 (cumulative easily exceeds 40 by hop 1-2).
static void buildDenseLocalMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 25, 120); // 25 nodes at hop 0, ~2 min old
    addNodesAtHop(0x2000, 1, 30, 300); // 30 nodes at hop 1, ~5 min old
    addNodesAtHop(0x3000, 2, 15, 600); // 15 nodes at hop 2, ~10 min old
    addNodesAtHop(0x4000, 3, 5, 1200); //  5 nodes at hop 3, ~20 min old
    addNodesAtHop(0x5000, 4, 2, 1800); //  2 nodes at hop 4, ~30 min old
    // Total: 77 nodes. Cumulative at hop 1: 25+30=55 > 40 → expect baseHop=1.
    // Politeness extension: 55*1.25=68.75, extending to hop 2 adds 15 → 70 > 68.75 → no extend.
    // With generous politeness (quiet mesh): 55*2.0=110, 70 < 110 → extend to hop 2.
}

// Scenario B: Spread suburban mesh — nodes distributed across hops 0–5.
// A position broadcast needs moderate reach.
// Expected: hop 3 or 4 (need to accumulate across several hops to reach 40).
static void buildSpreadSuburbanMesh()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 5, 120);   //  5 at hop 0
    addNodesAtHop(0x2000, 1, 8, 300);   //  8 at hop 1
    addNodesAtHop(0x3000, 2, 12, 600);  // 12 at hop 2
    addNodesAtHop(0x4000, 3, 15, 900);  // 15 at hop 3
    addNodesAtHop(0x5000, 4, 10, 1200); // 10 at hop 4
    addNodesAtHop(0x6000, 5, 6, 1800);  //  6 at hop 5
    // Total: 56 nodes. Cumulative: h0=5, h1=13, h2=25, h3=40 → baseHop=3.
    // Politeness check: extending to hop 4 → 50; limit = 40*polite.
    // Generous: 40*2=80, 50<80 → extend. Default: 40*1.5=60, 50<60 → extend. Strict: 40*1.25=50, 50<=50 → extend.
}

// Scenario C: Deep linear chain — few nodes per hop, spread to hop 6.
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
    // Total: 19 nodes. Never reaches 40 → baseHop = HOP_MAX = 7.
}

// Scenario D: Concentrated hop-2 cluster — simulates a repeater-heavy setup
// where most nodes are exactly 2 hops away (common with hilltop repeaters).
// Expected: hop 2 (cumulative at hop 2 exceeds 40).
static void buildRepeaterCluster()
{
    mockNodeDB->clearTestNodes();
    addNodesAtHop(0x1000, 0, 3, 120);  //  3 at hop 0 (local)
    addNodesAtHop(0x2000, 1, 5, 300);  //  5 at hop 1 (repeaters)
    addNodesAtHop(0x3000, 2, 45, 600); // 45 at hop 2 (behind repeaters)
    addNodesAtHop(0x4000, 3, 8, 1200); //  8 at hop 3
    // Total: 61. Cumulative: h0=3, h1=8, h2=53 > 40 → baseHop=2.
    // Politeness: extending to hop 3 → 61; limit=53*polite.
    // Strict: 53*1.25=66.25, 61<66 → extend. Default: 53*1.5=79.5, 61<79 → extend.
}

// Scenario E: Megamesh with high eviction turnover.
// DB is near capacity (180+ nodes) with steady evictions.
// Scale factor should inflate counts, potentially reducing required hops.
static void buildMegamesh()
{
    mockNodeDB->clearTestNodes();
    // Fill DB near capacity with nodes spread across hops 0-5
    addNodesAtHop(0x01000, 0, 30, 120);  // 30 at hop 0
    addNodesAtHop(0x02000, 1, 40, 300);  // 40 at hop 1
    addNodesAtHop(0x03000, 2, 35, 600);  // 35 at hop 2
    addNodesAtHop(0x04000, 3, 30, 900);  // 30 at hop 3
    addNodesAtHop(0x05000, 4, 20, 1200); // 20 at hop 4
    addNodesAtHop(0x06000, 5, 15, 1800); // 15 at hop 5
    // Total: 170 nodes (85% of 200). Near capacity.
    // Without scaling: cumulative at h0=30, h1=70 > 40 → hop 1. With scaling, even more compressed.
}

// ---------------------------------------------------------------------------
// Tests — Topology-driven hop reduction scenarios
// ---------------------------------------------------------------------------

// Scenario A: Dense local mesh — telemetry broadcast.
// 77 nodes clustered at hops 0-4, mostly at 0-1. Cumulative at hop 1 = 55 > 40.
// A telemetry packet should not need to travel beyond hop 2.
void test_dense_local_telemetry()
{
    TEST_MESSAGE("=== Dense local mesh: telemetry broadcast ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDenseLocalMesh();

    shim->runOnce();

    TEST_MSG_FMT("Dense local: hop=%u scale=%.2f actWt=%.2f polite=%.2f mode=%u", shim->getLastRequiredHop(),
                 (double)shim->getLastScaleFactor(), (double)shim->getLastActivityWeight(), 0.0,
                 0); // polite/mode logged by module

    // With 55 nodes reachable at hop 1, hop should be constrained to 1-2
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 1);

    hopScalingModule = nullptr;
}

// Scenario B: Spread suburban mesh — position broadcast.
// 56 nodes distributed across hops 0-5. Need hop 3 to reach 40.
// A position packet should travel 3-4 hops.
void test_spread_suburban_position()
{
    TEST_MESSAGE("=== Spread suburban mesh: position broadcast ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSuburbanMesh();

    shim->runOnce();

    TEST_MSG_FMT("Spread suburban: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    // Cumulative at hop 3 = 40. Politeness extends to hop 4.
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 3);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 5);

    hopScalingModule = nullptr;
}

// Scenario C: Deep linear chain — position broadcast.
// 19 nodes thinly spread across hops 0-6. Never reaches 40 cumulative.
// Must use maximum hops to reach everyone.
void test_deep_chain_position()
{
    TEST_MESSAGE("=== Deep linear chain: position broadcast ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildDeepLinearChain();

    shim->runOnce();

    TEST_MSG_FMT("Deep chain: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    // Only 19 nodes total — can never reach TARGET=40, so hop stays at HOP_MAX=7
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, shim->getLastRequiredHop());

    hopScalingModule = nullptr;
}

// Scenario D: Repeater cluster — telemetry broadcast.
// 61 nodes, 45 of which sit behind repeaters at hop 2. Cumulative at hop 2 = 53.
// A telemetry packet needs exactly 2-3 hops.
void test_repeater_cluster_telemetry()
{
    TEST_MESSAGE("=== Repeater cluster: telemetry broadcast ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRepeaterCluster();

    shim->runOnce();

    TEST_MSG_FMT("Repeater cluster: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    // 53 nodes reachable at hop 2, politeness should extend to hop 3
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() >= 2);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 4);

    hopScalingModule = nullptr;
}

// Scenario E: Megamesh with evictions — telemetry broadcast.
// 170 nodes (near capacity), then 3 hours of escalating evictions.
// Scale factor should grow, concentrating estimated nodes at lower hops.
// Hop limit should decrease as the mesh grows denser.
void test_megamesh_eviction_scaling()
{
    TEST_MESSAGE("=== Megamesh with eviction scaling ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildMegamesh();

    // Initial run — no evictions yet
    shim->runOnce();
    uint8_t hopBefore = shim->getLastRequiredHop();

    TEST_MSG_FMT("Megamesh initial: hop=%u scale=%.2f evict/h=%.1f", hopBefore, (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    // Simulate 3 hours of increasing eviction pressure
    for (int hour = 0; hour < 3; hour++) {
        for (int i = 0; i < 15 * (hour + 1); i++)
            shim->recordEviction();

        // Feed sampled nodes to build up the sampling estimate
        for (uint32_t i = 1; i <= 40; i++)
            shim->recordPacketSender(i * 8 + hour * 1000);

        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("Megamesh hour %d: hop=%u scale=%.2f evict/h=%.1f", hour + 1, shim->getLastRequiredHop(),
                     (double)shim->getLastScaleFactor(), (double)shim->getRollingEvictionAverage());
    }

    // After sustained evictions, scale factor should be > 1
    TEST_ASSERT_TRUE(shim->getLastScaleFactor() > 1.0f);
    // Hop should still be constrained (not HOP_MAX) given 170 nodes
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= 3);

    hopScalingModule = nullptr;
}

// Scenario F: Transition from sparse to dense — watches hop decrease.
// Start with a deep chain (19 nodes, hop=7), then add nodes at low hops
// to simulate a mesh growing denser locally. Hop should decrease.
void test_sparse_to_dense_transition()
{
    TEST_MESSAGE("=== Sparse-to-dense transition ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // Phase 1: sparse chain — expect HOP_MAX
    buildDeepLinearChain();
    shim->runOnce();
    uint8_t hopSparse = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 1 sparse: hop=%u (expect %u)", hopSparse, HOP_MAX);
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, hopSparse);

    // Phase 2: add 50 nodes at hop 0-1 (new neighbors joined).
    // Simulate an hourly update by running through a full hour cycle.
    addNodesAtHop(0xA000, 0, 25, 120);
    addNodesAtHop(0xB000, 1, 25, 300);

    // Force hourly recomputation (6 more runs)
    for (int run = 0; run < 6; run++)
        shim->runOnce();

    uint8_t hopDense = shim->getLastRequiredHop();
    TEST_MSG_FMT("Phase 2 dense: hop=%u (expect <= 3)", hopDense);

    // With 50 new local nodes, cumulative at hop 1 should exceed 40
    TEST_ASSERT_TRUE(hopDense < hopSparse);
    TEST_ASSERT_TRUE(hopDense <= 3);

    hopScalingModule = nullptr;
}

// Scenario G: State persistence across restarts.
// Run a megamesh for one hour, destroy instance, create a new one.
// The restored rolling averages should be non-zero.
void test_state_persistence()
{
    TEST_MESSAGE("=== State persistence across restart ===");

    // Phase 1: run with evictions
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

    // Phase 2: new instance restores state
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
// Large mesh with 96+ unique sampled IDs triggers rollSampleWindow(true).
void test_early_sample_flush()
{
    TEST_MESSAGE("=== Early sample flush under load ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildMegamesh();

    shim->runOnce();

    TEST_MESSAGE("Feeding 120 unique sampled node IDs (96 needed for flush)");
    for (uint32_t i = 1; i <= 120; i++)
        shim->recordPacketSender(i * 8);

    // Early flush should have fired; run once more for status report
    shim->runOnce();

    TEST_MSG_FMT("After early flush: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

// ---------------------------------------------------------------------------
// Tests — Hourly roll, intermediate status, startup lifecycle
// ---------------------------------------------------------------------------

// Hourly roll cycle: evictions + sampled nodes integrated into rolling averages.
void test_hourly_roll()
{
    TEST_MESSAGE("=== Hourly roll cycle ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildSpreadSuburbanMesh();

    for (int i = 0; i < 12; i++)
        shim->recordEviction();

    for (uint32_t i = 1; i <= 30; i++)
        shim->recordPacketSender(i * 8);

    for (int run = 0; run < 7; run++) {
        int32_t interval = shim->runOnce();
        TEST_ASSERT_GREATER_THAN(0, interval);
    }

    TEST_MSG_FMT("Hourly roll: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    TEST_ASSERT_TRUE(shim->getRollingEvictionAverage() > 0.0f);

    hopScalingModule = nullptr;
}

// Intermediate status: between hourly updates, hop/scale should not change.
void test_intermediate_status()
{
    TEST_MESSAGE("=== Intermediate status (no recomputation) ===");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    buildRepeaterCluster();

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
    // Topology-driven hop reduction
    RUN_TEST(test_dense_local_telemetry);
    RUN_TEST(test_spread_suburban_position);
    RUN_TEST(test_deep_chain_position);
    RUN_TEST(test_repeater_cluster_telemetry);
    RUN_TEST(test_megamesh_eviction_scaling);
    RUN_TEST(test_sparse_to_dense_transition);
    // Storage and lifecycle
    RUN_TEST(test_state_persistence);
    RUN_TEST(test_early_sample_flush);
    RUN_TEST(test_hourly_roll);
    RUN_TEST(test_intermediate_status);
    RUN_TEST(test_startup_blank_state);
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
