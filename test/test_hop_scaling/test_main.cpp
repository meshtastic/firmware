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

    /// Directly set denominator state, bypassing any scale-up/down logic.
    /// Used by tests that need a specific pre-condition without triggering trim.
    void forceFilterDenomState(uint8_t samp, uint8_t filt, uint8_t holdRolls)
    {
        samplingDenominator = samp;
        filteringDenominator = filt;
        filteringDenomHoldRollsRemaining = holdRolls;
    }
    uint8_t getFilteringDenomHoldRollsRemaining() const { return filteringDenomHoldRollsRemaining; }

    /// Insert an entry with an explicit hash, bypassing the sampling filter.
    /// Used to fill the histogram to a known state without depending on hashNodeId distribution.
    void forceInsertEntry(uint16_t hash, uint8_t hops)
    {
        if (count < CAPACITY) {
            entries[count].nodeHash = hash;
            entries[count].hops_away = hops;
            entries[count].seenHoursAgo = 1u;
            count++;
        }
    }

    // Size introspection for test_memory_layout
    static constexpr size_t sizeofSelf() { return sizeof(HopScalingModule); }
};

static MockNodeDB *mockNodeDB = nullptr;

// Create deterministic IDs that produce a broad spread of 16-bit hashes.
// HopScalingModule admission uses passesFilter(hashNodeId(nodeId), denom), NOT a raw nodeId
// modulo check — do not assume (nodeId & (denom-1)) == 0 determines whether a node is admitted.
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

    for (int run = 0; run < HopScalingModule::RUNS_PER_HOUR; run++)
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

// ---------------------------------------------------------------------------
// Tests — Denominator state machine
// ---------------------------------------------------------------------------

void test_denominator_rises_on_overflow()
{
    TEST_MESSAGE("=== samplingDenominator doubles when histogram overflows ===");
    TEST_MESSAGE("Fill to > FILL_HIGH_PCT with forceInsertEntry, then trigger via samplePacketForHistogram.");
    TEST_MESSAGE("Expectation: samp/filt both double to 2, hold set to FILTER_DENOM_HOLD_ROLLS.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // Insert 103 entries with hashes 1..103 (all distinct, no sampling-filter skew).
    // 103 / 128 = 80.4% fill, which meets FILL_HIGH_PCT=80.
    // Odd hashes (1,3,...,103) will be evicted when denom doubles to 2; even ones survive.
    static constexpr uint8_t FILL_COUNT = 103u;
    for (uint8_t i = 1; i <= FILL_COUNT; i++)
        shim->forceInsertEntry(i, 2u);

    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::DENOM_MIN, shim->getSamplingDenominator());
    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::DENOM_MIN, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(0u, shim->getFilteringDenomHoldRollsRemaining());
    TEST_ASSERT_EQUAL_UINT8(FILL_COUNT, shim->getEntryCount());

    // A new node passes the denom=1 admission gate; fill ≥ 80% triggers trimIfNeeded → doubling.
    shim->samplePacketForHistogram(0xB0000000u, 1u);

    TEST_MSG_FMT("After scale-up: samp=1/%u filt=1/%u holdRolls=%u entries=%u", shim->getSamplingDenominator(),
                 shim->getFilteringDenominator(), shim->getFilteringDenomHoldRollsRemaining(), shim->getEntryCount());

    TEST_ASSERT_EQUAL_UINT8(2u, shim->getSamplingDenominator());
    TEST_ASSERT_EQUAL_UINT8(2u, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::FILTER_DENOM_HOLD_ROLLS, shim->getFilteringDenomHoldRollsRemaining());
    // After evicting entries with (hash & 1) != 0, roughly half the entries remain.
    TEST_ASSERT_LESS_THAN_UINT8(FILL_COUNT, shim->getEntryCount());

    hopScalingModule = nullptr;
}

void test_filtering_denom_hold_counts_down()
{
    TEST_MESSAGE("=== filteringDenominator held while hold counter > 0 ===");
    TEST_MESSAGE("Force filt=4 samp=1 hold=3; verify no step for 2 rolls, then step fires on roll 3.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // samp=DENOM_MIN so scale-down in step 4 can't go lower; hold=3 for a short, fast test.
    shim->forceFilterDenomState(HopScalingModule::DENOM_MIN, 4u, 3u);

    shim->rollHourTest(); // hold 3→2, no step
    TEST_ASSERT_EQUAL_UINT8(4u, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(2u, shim->getFilteringDenomHoldRollsRemaining());

    shim->rollHourTest(); // hold 2→1, no step
    TEST_ASSERT_EQUAL_UINT8(4u, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(1u, shim->getFilteringDenomHoldRollsRemaining());

    // Roll 3: hold 1→0, step fires — filteringDenominator halves to max(2, samp=1) = 2.
    shim->rollHourTest();
    TEST_MSG_FMT("After hold expires: filt=1/%u samp=1/%u holdRolls=%u", shim->getFilteringDenominator(),
                 shim->getSamplingDenominator(), shim->getFilteringDenomHoldRollsRemaining());
    TEST_ASSERT_EQUAL_UINT8(2u, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(0u, shim->getFilteringDenomHoldRollsRemaining());

    hopScalingModule = nullptr;
}

void test_filtering_denom_steps_down_gradually()
{
    TEST_MESSAGE("=== filteringDenominator descends one halving per rollHour() after hold expires ===");
    TEST_MESSAGE("Force filt=8 samp=1 hold=1; expect 8→4→2→1 over 3 rolls, then stable.");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    shim->forceFilterDenomState(HopScalingModule::DENOM_MIN, 8u, 1u);

    shim->rollHourTest(); // hold 1→0, step: 8/2=4 > 1, filt=4
    TEST_ASSERT_EQUAL_UINT8(4u, shim->getFilteringDenominator());

    shim->rollHourTest(); // hold=0 (no decrement), step: 4/2=2 > 1, filt=2
    TEST_ASSERT_EQUAL_UINT8(2u, shim->getFilteringDenominator());

    shim->rollHourTest(); // step: 2/2=1, not > samp=1, filt=samp=1 — converged
    TEST_ASSERT_EQUAL_UINT8(1u, shim->getFilteringDenominator());

    shim->rollHourTest(); // filt==samp, outer if is false — no further change
    TEST_ASSERT_EQUAL_UINT8(1u, shim->getFilteringDenominator());
    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::DENOM_MIN, shim->getSamplingDenominator());

    hopScalingModule = nullptr;
}

void test_full_at_denom_max_drops_entry()
{
    TEST_MESSAGE("=== Full histogram at DENOM_MAX drops new entries ===");
    TEST_MESSAGE("Fill CAPACITY entries, force samp=DENOM_MAX, sample admissible node.");
    TEST_MESSAGE("Expectation: entry count stays at CAPACITY (LOG_WARN fires; visible in test output).");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();
    shim->setHashSeed(0); // deterministic hash for admissible-ID search

    shim->forceFilterDenomState(HopScalingModule::DENOM_MAX, HopScalingModule::DENOM_MAX, 0u);

    // Fill with odd hashes 1,3,5,...,(2*CAPACITY-1). None are multiples of 128, so none
    // collide with the admissible node's hash (which must be a multiple of 128).
    for (uint16_t i = 0; i < HopScalingModule::CAPACITY; i++)
        shim->forceInsertEntry(static_cast<uint16_t>(2u * i + 1u), 1u);

    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::CAPACITY, shim->getEntryCount());

    // Find a node ID whose hash passes DENOM_MAX, i.e. (hash & 127) == 0.
    uint32_t admissibleId = 0;
    for (uint32_t id = 1u; id < 0x10000u; id++) {
        if ((shim->hashNodeIdPublic(id) & (HopScalingModule::DENOM_MAX - 1u)) == 0u) {
            admissibleId = id;
            break;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0u, admissibleId); // sanity: the hash space is dense enough to find one quickly

    shim->samplePacketForHistogram(admissibleId, 3u);

    TEST_MSG_FMT("After drop attempt: entries=%u CAPACITY=%u admissibleId=0x%08x hash=0x%04x", shim->getEntryCount(),
                 static_cast<unsigned>(HopScalingModule::CAPACITY), admissibleId,
                 static_cast<unsigned>(shim->hashNodeIdPublic(admissibleId)));
    TEST_ASSERT_EQUAL_UINT8(HopScalingModule::CAPACITY, shim->getEntryCount());

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
    TEST_MESSAGE("");
    TEST_MESSAGE("=== Denominator state machine summary ===");
    TEST_MESSAGE("Test                              | Pre-condition                  | Expectation");
    TEST_MESSAGE("H: Rises on overflow              | 103 entries forced, denom=1    | samp/filt→2, holdRolls=13");
    TEST_MESSAGE(
        "I: Hold counts down               | filt=4 samp=1 hold=3           | no step for 2 rolls, step on roll 3: filt→2");
    TEST_MESSAGE("J: Steps down gradually           | filt=8 samp=1 hold=1           | 8→4→2→1 over 3 rolls, stable on 4th");
    TEST_MESSAGE("K: Full at DENOM_MAX drops entry  | 128 entries, samp=filt=128     | count stays 128, LOG_WARN emitted");
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
    RUN_TEST(test_denominator_rises_on_overflow);
    RUN_TEST(test_filtering_denom_hold_counts_down);
    RUN_TEST(test_filtering_denom_steps_down_gradually);
    RUN_TEST(test_full_at_denom_max_drops_entry);
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