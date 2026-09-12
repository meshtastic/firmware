// Unit tests for directed routine sends: destination selection, reply policy and hop budget.
#include "Default.h"
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/Telemetry/BaseTelemetryModule.h"
#include <unity.h>

static NodeDB *testNodeDB = nullptr;

namespace
{
constexpr NodeNum LOCAL_NODE = 0x11111111;
constexpr NodeNum POLLER = 0x22222222;
constexpr NodeNum OTHER = 0x33333333;
constexpr NodeNum DEST = 0x44444444;

using Flags = meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags;

void clearFlags()
{
    moduleConfig.telemetry.telemetry_flags = 0;
}

/// Put `num` in the node DB with the given attributes, returning the entry.
meshtastic_NodeInfoLite *ensureNode(NodeNum num, bool favorite = false, bool ignored = false)
{
    meshtastic_NodeInfoLite *n = nodeDB->getOrCreateMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_FAVORITE_MASK, favorite);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_IGNORED_MASK, ignored);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_VIA_MQTT_MASK, false);
    n->has_hops_away = false;
    n->hops_away = 0;
    return n;
}
} // namespace

// --- routineDest: 0 means broadcast, anything else addresses that node -------------------------

void test_routineDest_zeroIsBroadcast()
{
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, BaseTelemetryModule::routineDest(0));
}

void test_routineDest_nonZeroIsThatNode()
{
    TEST_ASSERT_EQUAL_UINT32(DEST, BaseTelemetryModule::routineDest(DEST));
}

// --- wouldReplyToPoll --------------------------------------------------------------------------

// Unset flags must behave exactly as the firmware did before this feature: answer everyone.
void test_reply_unsetAnswersEveryone()
{
    clearFlags();
    ensureNode(POLLER);
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
    // Also true for a node we have never heard of.
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(0x5a5a5a5a, 0));
}

void test_reply_noAdhocRefusesEveryone()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NO_ADHOC_REPLY;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

void test_reply_onlyToDest()
{
    clearFlags();
    ensureNode(POLLER);
    ensureNode(OTHER);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, POLLER));
    // With no destination configured the restriction cannot be satisfied by anyone.
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
}

void test_reply_favouritesOnly()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true);
    ensureNode(OTHER, /*favorite=*/false);
    moduleConfig.telemetry.telemetry_flags =
        Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_TO_FAVOURITES_ONLY;
    TEST_ASSERT_TRUE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(OTHER, 0));
    // A node not in the DB at all is not a favourite - isFavorite() must fail closed.
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(0x5a5a5a5a, 0));
}

// Ignoring a node is not a policy choice, so it must hold even with no flags set.
void test_reply_ignoredRefusedEvenWithFlagsUnset()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/false, /*ignored=*/true);
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, 0));
}

// An ignored node stays refused even when it is also the configured destination.
void test_reply_ignoredBeatsDest()
{
    clearFlags();
    ensureNode(POLLER, /*favorite=*/true, /*ignored=*/true);
    moduleConfig.telemetry.telemetry_flags = Flags::meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST;
    TEST_ASSERT_FALSE(BaseTelemetryModule::wouldReplyToPoll(POLLER, POLLER));
}

// --- hopLimitForDirected -----------------------------------------------------------------------

void test_hop_unknownDistanceUsesConfigured()
{
    ensureNode(DEST); // has_hops_away stays false
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
    // A node absent from the DB likewise has no basis to trim.
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(0x5a5a5a5a, 3));
}

void test_hop_directNeighbourKeepsReturnMargin()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    // 0 + 2 < 5, so trim to 2 - the margin survives even for a direct neighbour.
    TEST_ASSERT_EQUAL_UINT8(2, hopLimitForDirected(DEST, 5));
}

// The boundary an off-by-one would flip: 1 + 2 is not < 3, so the configured cap applies.
void test_hop_boundaryAtConfiguredLimit()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 1;
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
    // With more room the same distance does trim.
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 7));
}

void test_hop_neverExceedsConfigured()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 6;
    TEST_ASSERT_EQUAL_UINT8(3, hopLimitForDirected(DEST, 3));
}

// hops_away is stored without a via_mqtt guard, so a distance learned over MQTT may not describe a
// LoRa path at all. It must not be used to size a LoRa hop budget.
void test_hop_mqttDistanceIsNotTrusted()
{
    meshtastic_NodeInfoLite *n = ensureNode(DEST);
    n->has_hops_away = true;
    n->hops_away = 0;
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_VIA_MQTT_MASK, true);
    TEST_ASSERT_EQUAL_UINT8(5, hopLimitForDirected(DEST, 5));
}

void setUp(void)
{
    if (!testNodeDB)
        testNodeDB = new NodeDB();
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE;
    nodeDB = testNodeDB;
}

void tearDown(void) {}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_routineDest_zeroIsBroadcast);
    RUN_TEST(test_routineDest_nonZeroIsThatNode);
    RUN_TEST(test_reply_unsetAnswersEveryone);
    RUN_TEST(test_reply_noAdhocRefusesEveryone);
    RUN_TEST(test_reply_onlyToDest);
    RUN_TEST(test_reply_favouritesOnly);
    RUN_TEST(test_reply_ignoredRefusedEvenWithFlagsUnset);
    RUN_TEST(test_reply_ignoredBeatsDest);
    RUN_TEST(test_hop_unknownDistanceUsesConfigured);
    RUN_TEST(test_hop_directNeighbourKeepsReturnMargin);
    RUN_TEST(test_hop_boundaryAtConfiguredLimit);
    RUN_TEST(test_hop_neverExceedsConfigured);
    RUN_TEST(test_hop_mqttDistanceIsNotTrusted);
    exit(UNITY_END());
}

void loop() {}
