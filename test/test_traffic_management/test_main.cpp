#include "TestUtil.h"
#include <unity.h>

#if HAS_TRAFFIC_MANAGEMENT

#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/TrafficManagementModule.h"
#include <pb_encode.h>

namespace
{

constexpr NodeNum kLocalNode = 0x11111111;
constexpr NodeNum kRemoteNode = 0x22222222;
constexpr NodeNum kTargetNode = 0x33333333;

class MockNodeDB : public NodeDB
{
  public:
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) override
    {
        if (hasCachedNode && n == cachedNodeNum)
            return &cachedNode;
        return nullptr;
    }

    void clearCachedNode()
    {
        hasCachedNode = false;
        cachedNodeNum = 0;
        cachedNode = meshtastic_NodeInfoLite_init_zero;
    }

    void setCachedNode(NodeNum n)
    {
        clearCachedNode();
        hasCachedNode = true;
        cachedNodeNum = n;
        cachedNode.num = n;
        cachedNode.has_user = true;
    }

  private:
    bool hasCachedNode = false;
    NodeNum cachedNodeNum = 0;
    meshtastic_NodeInfoLite cachedNode = meshtastic_NodeInfoLite_init_zero;
};

class TrafficManagementModuleTestShim : public TrafficManagementModule
{
  public:
    using TrafficManagementModule::alterReceived;
    using TrafficManagementModule::handleReceived;

    bool ignoreRequestFlag() const { return ignoreRequest; }
};

MockNodeDB *mockNodeDB = nullptr;

static void resetTrafficConfig()
{
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    moduleConfig.has_traffic_management = true;
    moduleConfig.traffic_management = meshtastic_ModuleConfig_TrafficManagementConfig_init_zero;
    moduleConfig.traffic_management.enabled = true;

    config = meshtastic_LocalConfig_init_zero;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    myNodeInfo.my_node_num = kLocalNode;

    router = nullptr;
    service = nullptr;

    mockNodeDB->clearCachedNode();
    nodeDB = mockNodeDB;
}

static meshtastic_MeshPacket makeDecodedPacket(meshtastic_PortNum port, NodeNum from, NodeNum to = NODENUM_BROADCAST)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = from;
    packet.to = to;
    packet.id = 0x1001;
    packet.channel = 0;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = port;
    packet.decoded.has_bitfield = true;
    packet.decoded.bitfield = 0;
    return packet;
}

static meshtastic_MeshPacket makeUnknownPacket(NodeNum from, NodeNum to = NODENUM_BROADCAST)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = from;
    packet.to = to;
    packet.id = 0x2001;
    packet.channel = 0;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.encrypted.size = 0;
    return packet;
}

static meshtastic_MeshPacket makePositionPacket(NodeNum from, int32_t lat, int32_t lon, NodeNum to = NODENUM_BROADCAST)
{
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, from, to);
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.has_longitude_i = true;
    pos.latitude_i = lat;
    pos.longitude_i = lon;

    packet.decoded.payload.size =
        pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes), &meshtastic_Position_msg, &pos);
    return packet;
}

static void test_tm_moduleDisabled_doesNothing(void)
{
    moduleConfig.has_traffic_management = false;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);

    ProcessMessage result = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(0, stats.packets_inspected);
    TEST_ASSERT_EQUAL_UINT32(0, stats.unknown_packet_drops);
    TEST_ASSERT_FALSE(module.ignoreRequestFlag());
}

static void test_tm_unknownPackets_dropOnNPlusOne(void)
{
    moduleConfig.traffic_management.drop_unknown_enabled = true;
    moduleConfig.traffic_management.unknown_packet_threshold = 2;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    ProcessMessage r3 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unknown_packet_drops);
    TEST_ASSERT_EQUAL_UINT32(3, stats.packets_inspected);
    TEST_ASSERT_TRUE(module.ignoreRequestFlag());
}

static void test_tm_positionDedup_dropsDuplicateWithinWindow(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 16;
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_GREATER_THAN_UINT32(0, first.decoded.payload.size);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
    TEST_ASSERT_TRUE(module.ignoreRequestFlag());
}

static void test_tm_positionDedup_allowsMovedPosition(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 16;
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket moved = makePositionPacket(kRemoteNode, 384221234, -1210845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(moved);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
}

static void test_tm_rateLimit_dropsOnlyAfterThreshold(void)
{
    moduleConfig.traffic_management.rate_limit_enabled = true;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 3;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    ProcessMessage r3 = module.handleReceived(packet);
    ProcessMessage r4 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r4));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rate_limit_drops);
    TEST_ASSERT_TRUE(module.ignoreRequestFlag());
}

static void test_tm_rateLimit_skipsRoutingAndAdminPorts(void)
{
    moduleConfig.traffic_management.rate_limit_enabled = true;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket routingPacket = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode);
    meshtastic_MeshPacket adminPacket = makeDecodedPacket(meshtastic_PortNum_ADMIN_APP, kRemoteNode);

    for (int i = 0; i < 4; i++) {
        ProcessMessage rr = module.handleReceived(routingPacket);
        ProcessMessage ar = module.handleReceived(adminPacket);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(rr));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(ar));
    }

    meshtastic_TrafficManagementStats stats = module.getStats();
    TEST_ASSERT_EQUAL_UINT32(0, stats.rate_limit_drops);
}

static void test_tm_fromUs_bypassesPositionAndRateFilters(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 16;
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    moduleConfig.traffic_management.rate_limit_enabled = true;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket positionPacket = makePositionPacket(kLocalNode, 374221234, -1220845678);
    meshtastic_MeshPacket textPacket = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode);

    ProcessMessage p1 = module.handleReceived(positionPacket);
    ProcessMessage p2 = module.handleReceived(positionPacket);
    ProcessMessage t1 = module.handleReceived(textPacket);
    ProcessMessage t2 = module.handleReceived(textPacket);

    meshtastic_TrafficManagementStats stats = module.getStats();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(p1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(p2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(t1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(t2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
    TEST_ASSERT_EQUAL_UINT32(0, stats.rate_limit_drops);
}

static void test_tm_nodeinfo_routerClamp_skipsWhenTooManyHops(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response = true;
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    mockNodeDB->setCachedNode(kTargetNode);

    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, kRemoteNode, kTargetNode);
    request.decoded.want_response = true;
    request.hop_start = 5;
    request.hop_limit = 1; // 4 hops away; router clamp should cap max at 3

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(0, stats.nodeinfo_cache_hits);
    TEST_ASSERT_FALSE(module.ignoreRequestFlag());
}

static void test_tm_nodeinfo_clientClamp_skipsWhenNotDirect(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response = true;
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    mockNodeDB->setCachedNode(kTargetNode);

    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, kRemoteNode, kTargetNode);
    request.decoded.want_response = true;
    request.hop_start = 2;
    request.hop_limit = 1; // 1 hop away; clients are clamped to max 0

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(0, stats.nodeinfo_cache_hits);
    TEST_ASSERT_FALSE(module.ignoreRequestFlag());
}

static void test_tm_alterReceived_exhaustsRelayedTelemetryBroadcast(void)
{
    moduleConfig.traffic_management.exhaust_hop_telemetry = true;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 3;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(0, packet.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(3, packet.hop_start);
    TEST_ASSERT_TRUE(module.shouldExhaustHops());
    TEST_ASSERT_EQUAL_UINT32(1, stats.hop_exhausted_packets);
}

static void test_tm_alterReceived_skipsLocalAndUnicast(void)
{
    moduleConfig.traffic_management.exhaust_hop_telemetry = true;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket unicast = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kTargetNode);
    unicast.hop_start = 5;
    unicast.hop_limit = 3;
    module.alterReceived(unicast);
    TEST_ASSERT_EQUAL_UINT8(3, unicast.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops());

    meshtastic_MeshPacket fromUs = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kLocalNode, NODENUM_BROADCAST);
    fromUs.hop_start = 5;
    fromUs.hop_limit = 3;
    module.alterReceived(fromUs);
    TEST_ASSERT_EQUAL_UINT8(3, fromUs.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops());

    meshtastic_TrafficManagementStats stats = module.getStats();
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

} // namespace

void setUp(void)
{
    resetTrafficConfig();
}
void tearDown(void) {}

extern "C" void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    mockNodeDB = new MockNodeDB();
    nodeDB = mockNodeDB;

    UNITY_BEGIN();
    RUN_TEST(test_tm_moduleDisabled_doesNothing);
    RUN_TEST(test_tm_unknownPackets_dropOnNPlusOne);
    RUN_TEST(test_tm_positionDedup_dropsDuplicateWithinWindow);
    RUN_TEST(test_tm_positionDedup_allowsMovedPosition);
    RUN_TEST(test_tm_rateLimit_dropsOnlyAfterThreshold);
    RUN_TEST(test_tm_rateLimit_skipsRoutingAndAdminPorts);
    RUN_TEST(test_tm_fromUs_bypassesPositionAndRateFilters);
    RUN_TEST(test_tm_nodeinfo_routerClamp_skipsWhenTooManyHops);
    RUN_TEST(test_tm_nodeinfo_clientClamp_skipsWhenNotDirect);
    RUN_TEST(test_tm_alterReceived_exhaustsRelayedTelemetryBroadcast);
    RUN_TEST(test_tm_alterReceived_skipsLocalAndUnicast);
    exit(UNITY_END());
}

extern "C" void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}

extern "C" void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

extern "C" void loop() {}

#endif
