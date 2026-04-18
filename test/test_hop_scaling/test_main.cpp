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
// Helpers
// ---------------------------------------------------------------------------

static void populateSmallMesh()
{
    mockNodeDB->clearTestNodes();

    for (uint32_t i = 0; i < 10; i++)
        mockNodeDB->addTestNode(0x1000 + i, 0, true, 300 + i); // hop 0, ~5 min old

    for (uint32_t i = 0; i < 8; i++)
        mockNodeDB->addTestNode(0x2000 + i, 1, true, 600 + i); // hop 1, ~10 min old

    for (uint32_t i = 0; i < 5; i++)
        mockNodeDB->addTestNode(0x3000 + i, 2, true, 1800 + i); // hop 2, ~30 min old

    for (uint32_t i = 0; i < 6; i++)
        mockNodeDB->addTestNode(0x4000 + i, 1, true, 4500 + i); // hop 1, ~75 min old

    for (uint32_t i = 0; i < 4; i++)
        mockNodeDB->addTestNode(0x5000 + i, 2, true, 8100 + i); // hop 2, ~135 min old
}

static void populateLargeMesh()
{
    mockNodeDB->clearTestNodes();

    for (uint32_t i = 0; i < 100; i++) {
        uint8_t hop = (i < 40) ? 0 : (i < 70) ? 1 : (i < 90) ? 2 : 3;
        mockNodeDB->addTestNode(0x10000 + i, hop, true, 600 + i);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_startup_blank_state()
{
    TEST_MESSAGE("--- test_startup_blank_state ---");

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    populateSmallMesh();

    int32_t interval = shim->runOnce();

    TEST_ASSERT_GREATER_THAN(0, interval);
    TEST_ASSERT_TRUE(shim->getLastRequiredHop() <= HOP_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, shim->getRollingEvictionAverage());

    TEST_MSG_FMT("startup blank: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    hopScalingModule = nullptr;
}

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

        TEST_ASSERT_GREATER_THAN(0.0f, shim->getRollingEvictionAverage());

        hopScalingModule = nullptr;
    }
}

void test_early_sample_flush()
{
    TEST_MESSAGE("--- test_early_sample_flush ---");

    populateLargeMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    shim->runOnce();

    TEST_MESSAGE("feeding 96 unique sampled node IDs to trigger early flush");
    for (uint32_t i = 1; i <= 120; i++) {
        shim->recordPacketSender(i * 8);
    }

    shim->runOnce();

    TEST_MSG_FMT("early flush done: hop=%u scale=%.2f", shim->getLastRequiredHop(), (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

void test_hourly_roll()
{
    TEST_MESSAGE("--- test_hourly_roll ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    for (int i = 0; i < 12; i++)
        shim->recordEviction();

    for (uint32_t i = 1; i <= 30; i++)
        shim->recordPacketSender(i * 8);

    for (int run = 0; run < 7; run++) {
        int32_t interval = shim->runOnce();
        TEST_ASSERT_GREATER_THAN(0, interval);
    }

    TEST_MSG_FMT("hourly roll: hop=%u actWt=%.2f scale=%.2f evict/h=%.1f", shim->getLastRequiredHop(),
                 (double)shim->getLastActivityWeight(), (double)shim->getLastScaleFactor(),
                 (double)shim->getRollingEvictionAverage());

    TEST_ASSERT_GREATER_THAN(0.0f, shim->getRollingEvictionAverage());

    hopScalingModule = nullptr;
}

void test_intermediate_status()
{
    TEST_MESSAGE("--- test_intermediate_status ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    shim->runOnce();
    uint8_t hopAfterInitial = shim->getLastRequiredHop();
    float scaleAfterInitial = shim->getLastScaleFactor();

    TEST_MSG_FMT("initial: hop=%u scale=%.2f", hopAfterInitial, (double)scaleAfterInitial);

    for (int run = 0; run < 3; run++) {
        shim->runOnce();
        TEST_ASSERT_EQUAL_UINT8(hopAfterInitial, shim->getLastRequiredHop());
        TEST_ASSERT_FLOAT_WITHIN(0.001f, scaleAfterInitial, shim->getLastScaleFactor());
    }

    TEST_MSG_FMT("intermediate (x3): hop=%u scale=%.2f (unchanged)", shim->getLastRequiredHop(),
                 (double)shim->getLastScaleFactor());

    hopScalingModule = nullptr;
}

void test_multiple_hourly_rolls_with_evictions()
{
    TEST_MESSAGE("--- test_multiple_hourly_rolls_with_evictions ---");

    populateSmallMesh();

    auto shim = std::unique_ptr<HopScalingTestShim>(new HopScalingTestShim());
    hopScalingModule = shim.get();

    float prevEvictionAvg = 0.0f;

    for (int hour = 0; hour < 3; hour++) {
        for (int i = 0; i < 10 * (hour + 1); i++)
            shim->recordEviction();

        for (int run = 0; run < 7; run++)
            shim->runOnce();

        TEST_MSG_FMT("hour %d: hop=%u scale=%.2f evict/h=%.1f", hour + 1, shim->getLastRequiredHop(),
                     (double)shim->getLastScaleFactor(), (double)shim->getRollingEvictionAverage());

        TEST_ASSERT_GREATER_OR_EQUAL(prevEvictionAvg, shim->getRollingEvictionAverage());
        prevEvictionAvg = shim->getRollingEvictionAverage();
    }

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
    delay(10);
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
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif
