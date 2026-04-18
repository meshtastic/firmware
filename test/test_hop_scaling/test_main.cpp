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

namespace
{

constexpr NodeNum kLocalNode = 0x11111111;

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

    /// Add a test node with controlled hops_away and last_heard age.
    void addTestNode(NodeNum num, uint8_t hopsAway, bool hasHops, uint32_t ageSecs, bool viaMqtt = false)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.has_hops_away = hasHops;
        node.hops_away = hopsAway;
        node.via_mqtt = viaMqtt;
        node.last_heard = getTime() - ageSecs;
        testNodes.push_back(node);
        // Point the base class at our vector.
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

// ---------------------------------------------------------------------------
// Test shim — expose private/protected methods for direct invocation
// ---------------------------------------------------------------------------
class HopScalingTestShim : public HopScalingModule
{
  public:
    using HopScalingModule::recordEviction;
    using HopScalingModule::recordPacketSender;
    using HopScalingModule::runOnce;

    // Accessors for state inspection
    using HopScalingModule::getEvictionsCurrentHour;
    using HopScalingModule::getLastActivityWeight;
    using HopScalingModule::getLastRequiredHop;
    using HopScalingModule::getLastScaleFactor;
    using HopScalingModule::getRollingEvictionAverage;
};

MockNodeDB *mockNodeDB = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Populate a small mesh: 10 nodes at hop 0 (recent), 8 at hop 1, 5 at hop 2,
/// plus some at 1-2h and 2-3h age.
static void populateSmallMesh()
{
    mockNodeDB->clearTestNodes();

    // Recent 1h — hop 0
    for (uint32_t i = 0; i < 10; i++)
        mockNodeDB->addTestNode(0x1000 + i, 0, true, 300 + i); // ~5 min old

    // Recent 1h — hop 1
    for (uint32_t i = 0; i < 8; i++)
        mockNodeDB->addTestNode(0x2000 + i, 1, true, 600 + i); // ~10 min old

    // Recent 1h — hop 2
    for (uint32_t i = 0; i < 5; i++)
        mockNodeDB->addTestNode(0x3000 + i, 2, true, 1800 + i); // ~30 min old

    // 1-2h old — hop 1 (for activity weight computation)
    for (uint32_t i = 0; i < 6; i++)
        mockNodeDB->addTestNode(0x4000 + i, 1, true, 4500 + i); // ~75 min old

    // 2-3h old — hop 2 (for activity weight computation)
    for (uint32_t i = 0; i < 4; i++)
        mockNodeDB->addTestNode(0x5000 + i, 2, true, 8100 + i); // ~135 min old
}

/// Populate a large mesh that will saturate the sample tracker.
/// Uses node IDs that are multiples of 8 (default SAMPLING_DENOMINATOR)
/// so they pass the modulo filter.
static void populateLargeMesh()
{
    mockNodeDB->clearTestNodes();

    // 100 nodes across several hops, all recent
    for (uint32_t i = 0; i < 100; i++) {
        uint8_t hop = (i < 40) ? 0 : (i < 70) ? 1 : (i < 90) ? 2 : 3;
        mockNodeDB->addTestNode(0x10000 + i, hop, true, 600 + i);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// Test 1: Startup with blank state (no persisted file).
/// Should log the initial status report with default values.
void test_startup_blank_state()
{
    TEST_MESSAGE("--- test_startup_blank_state ---");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    populateSmallMesh();

    // First runOnce = initial run, builds snapshot and computes hop.
    int32_t interval = shim->runOnce();

    TEST_ASSERT_GREATER_THAN(0, interval);
    // With a small mesh and no evictions, should be in startup or small-stable mode
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= HOP_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, shim->getRollingEvictionAverage());

    TEST_MSG_FMT("startup blank: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    hopScalingModule = nullptr;
}

/// Test 2: Startup with pre-existing state (simulated restore).
/// We create an instance, feed it some evictions, run it through one hour,
/// then create a second instance that should restore the persisted state.
void test_startup_restored_state()
{
    TEST_MESSAGE("--- test_startup_restored_state ---");

    populateSmallMesh();

    // Phase 1: run first instance, add evictions, roll one hour
    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();

        for (int i = 0; i < 5; i++)
            shim->recordEviction();

        // Run 7 times to trigger an hourly roll (1 initial + 6 more = hour boundary)
        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("phase 1 done: evict/h=%.1f", (double)shim->getRollingEvictionAverage());
        TEST_ASSERT_GREATER_THAN(0.0f, shim->getRollingEvictionAverage());

        hopScalingModule = nullptr;
    }

    // Phase 2: create new instance — should restore from persisted state
    {
        auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
        hopScalingModule = shim.get();

        shim->runOnce();

        TEST_MSG_FMT("phase 2 restored: evict/h=%.1f hop=%u scale=%.2f", (double)shim->getRollingEvictionAverage(),
                     shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

        // The restored eviction average should be non-zero from phase 1
        TEST_ASSERT_GREATER_THAN(0.0f, shim->getRollingEvictionAverage());

        hopScalingModule = nullptr;
    }
}

/// Test 3: Early flush of the sampling tracker.
/// Feed enough packet senders to hit the 75% load cap and trigger rollSampleWindow(true).
void test_early_sample_flush()
{
    TEST_MESSAGE("--- test_early_sample_flush ---");

    populateLargeMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // Initial run to set up state
    shim->runOnce();

    // Feed node IDs that pass the modulo-8 filter. We need 96 unique IDs
    // (SAMPLE_TRACKER_LOAD_CAP) to trigger the early flush.
    // Use IDs that are multiples of 8.
    TEST_MESSAGE("feeding 96 unique sampled node IDs to trigger early flush");
    for (uint32_t i = 1; i <= 120; i++) {
        shim->recordPacketSender(i * 8); // multiples of 8 pass modulo-8 filter
    }

    // The early flush should have already fired inside recordPacketSender
    // when uniqueCount hit 96. Run once more to see the status report.
    shim->runOnce();

    TEST_MSG_FMT("early flush done: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

/// Test 4: Full hourly roll cycle.
/// Run through 7 iterations (initial + 6 ten-minute runs) to trigger rollHour().
void test_hourly_roll()
{
    TEST_MESSAGE("--- test_hourly_roll ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // Add some evictions during the "hour"
    for (int i = 0; i < 12; i++)
        shim->recordEviction();

    // Feed some sampled nodes
    for (uint32_t i = 1; i <= 30; i++)
        shim->recordPacketSender(i * 8);

    // Run 7 times: first run is initial (no roll), then 6 more trigger hourly roll
    for (int run = 0; run < 7; run++) {
        int32_t interval = shim->runOnce();
        TEST_ASSERT_GREATER_THAN(0, interval);
    }

    TEST_MSG_FMT("hourly roll: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    // After rolling, eviction average should reflect the 12 evictions we added
    TEST_ASSERT_GREATER_THAN(0.0f, shim->getRollingEvictionAverage());

    hopScalingModule = nullptr;
}

/// Test 5: Intermediate status update (non-hourly run).
/// After the initial run, the next few runs should emit status reports
/// without recomputing hop/scale (didHourlyUpdate = false).
void test_intermediate_status()
{
    TEST_MESSAGE("--- test_intermediate_status ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    // Initial run — does hourly computation
    shim->runOnce();
    uint8_t hopAfterInitial = shim->getLastRequiredHop();
    float scaleAfterInitial = shim->getLastScaleFactor();

    TEST_MSG_FMT("initial: hop=%u scale=%.2f", hopAfterInitial, (double)scaleAfterInitial);

    // Next few runs are intermediate (no hourly update) — hop and scale should not change
    for (int run = 0; run < 3; run++) {
        shim->runOnce();
        TEST_ASSERT_EQUAL_UINT8(hopAfterInitial, shim->getLastRequiredHop());
        TEST_ASSERT_FLOAT_WITHIN(0.001f, scaleAfterInitial, shim->getLastScaleFactor());
    }

    TEST_MSG_FMT("intermediate (x3): hop=%u scale=%.2f (unchanged)", shim->getLastRequiredHop(),
                 (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

/// Test 6: Multiple hourly rolls with growing evictions to show scale factor increase.
void test_multiple_hourly_rolls_with_evictions()
{
    TEST_MESSAGE("--- test_multiple_hourly_rolls_with_evictions ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    float prevEvictionAvg = 0.0f;

    // Simulate 3 hourly cycles
    for (int hour = 0; hour < 3; hour++) {
        // Add increasing evictions each hour
        for (int i = 0; i < 10 * (hour + 1); i++)
            shim->recordEviction();

        // Run 7 times per hour (initial + 6)
        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("hour %d: hop=%u scale=%.2f evict/h=%.1f", hour + 1, shim->getLastRequiredHop(),
                     (double)shim->getLastScaleFactor(), (double)shim->getRollingEvictionAverage());

        // Eviction average should be growing
        TEST_ASSERT_GREATER_OR_EQUAL(prevEvictionAvg, shim->getRollingEvictionAverage());
        prevEvictionAvg = shim->getRollingEvictionAverage();
    }

    hopScalingModule = nullptr;
}

} // namespace

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
    RUN_TEST(test_startup_blank_state);
    RUN_TEST(test_startup_restored_state);
    RUN_TEST(test_early_sample_flush);
    RUN_TEST(test_hourly_roll);
    RUN_TEST(test_intermediate_status);
    RUN_TEST(test_multiple_hourly_rolls_with_evictions);
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
