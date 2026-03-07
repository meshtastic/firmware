#include "MeshModule.h"
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

// Minimal concrete subclass for testing the base class helper
class TestModule : public MeshModule
{
  public:
    TestModule() : MeshModule("TestModule") {}
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    using MeshModule::currentRequest;
    using MeshModule::isMultiHopBroadcastRequest;
};

static TestModule *testModule;
static meshtastic_MeshPacket testPacket;

void setUp(void)
{
    testModule = new TestModule();
    memset(&testPacket, 0, sizeof(testPacket));
    TestModule::currentRequest = &testPacket;
}

void tearDown(void)
{
    TestModule::currentRequest = NULL;
    delete testModule;
}

// Zero-hop broadcast (hop_limit == hop_start): should be allowed
static void test_zeroHopBroadcast_isAllowed()
{
    testPacket.to = NODENUM_BROADCAST;
    testPacket.hop_start = 3;
    testPacket.hop_limit = 3; // Not yet relayed

    TEST_ASSERT_FALSE(testModule->isMultiHopBroadcastRequest());
}

// Multi-hop broadcast (hop_limit < hop_start): should be blocked
static void test_multiHopBroadcast_isBlocked()
{
    testPacket.to = NODENUM_BROADCAST;
    testPacket.hop_start = 7;
    testPacket.hop_limit = 4; // Already relayed 3 hops

    TEST_ASSERT_TRUE(testModule->isMultiHopBroadcastRequest());
}

// Direct message (not broadcast): should always be allowed regardless of hops
static void test_directMessage_isAllowed()
{
    testPacket.to = 0x12345678; // Specific node
    testPacket.hop_start = 7;
    testPacket.hop_limit = 4;

    TEST_ASSERT_FALSE(testModule->isMultiHopBroadcastRequest());
}

// Broadcast with hop_limit == 0 (fully relayed): should be blocked
static void test_fullyRelayedBroadcast_isBlocked()
{
    testPacket.to = NODENUM_BROADCAST;
    testPacket.hop_start = 3;
    testPacket.hop_limit = 0;

    TEST_ASSERT_TRUE(testModule->isMultiHopBroadcastRequest());
}

// No current request: should not crash, should return false
static void test_noCurrentRequest_isAllowed()
{
    TestModule::currentRequest = NULL;

    TEST_ASSERT_FALSE(testModule->isMultiHopBroadcastRequest());
}

// Broadcast with hop_start == 0 (legacy or local): should be allowed
static void test_legacyPacket_zeroHopStart_isAllowed()
{
    testPacket.to = NODENUM_BROADCAST;
    testPacket.hop_start = 0;
    testPacket.hop_limit = 0;

    // hop_limit == hop_start, so not multi-hop
    TEST_ASSERT_FALSE(testModule->isMultiHopBroadcastRequest());
}

// Single hop relayed broadcast (hop_limit = hop_start - 1): should be blocked
static void test_singleHopRelayedBroadcast_isBlocked()
{
    testPacket.to = NODENUM_BROADCAST;
    testPacket.hop_start = 3;
    testPacket.hop_limit = 2;

    TEST_ASSERT_TRUE(testModule->isMultiHopBroadcastRequest());
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_zeroHopBroadcast_isAllowed);
    RUN_TEST(test_multiHopBroadcast_isBlocked);
    RUN_TEST(test_directMessage_isAllowed);
    RUN_TEST(test_fullyRelayedBroadcast_isBlocked);
    RUN_TEST(test_noCurrentRequest_isAllowed);
    RUN_TEST(test_legacyPacket_zeroHopStart_isAllowed);
    RUN_TEST(test_singleHopRelayedBroadcast_isBlocked);
    exit(UNITY_END());
}

void loop() {}
