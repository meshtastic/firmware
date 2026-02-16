#include "TestUtil.h"
#include <unity.h>

#if HAS_TRAFFIC_MANAGEMENT

#include "mesh/CryptoEngine.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/TrafficManagementModule.h"
#include <climits>
#include <memory>
#include <pb_encode.h>
#include <vector>

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

class MockRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }
};

class MockRouter : public Router
{
  public:
    ~MockRouter()
    {
        // Router allocates a global crypt lock in its constructor.
        // Clean it up here so each test can build a fresh mock router.
        delete cryptLock;
        cryptLock = nullptr;
    }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sentPackets.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
};

class TrafficManagementModuleTestShim : public TrafficManagementModule
{
  public:
    using TrafficManagementModule::alterReceived;
    using TrafficManagementModule::handleReceived;
    using TrafficManagementModule::runOnce;

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

/**
 * Verify the module is a no-op when traffic management is disabled.
 * Important so config toggles cannot accidentally change routing behavior.
 */
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

/**
 * Verify unknown-packet dropping uses N+1 threshold semantics.
 * Important to catch off-by-one regressions in drop decisions.
 */
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

/**
 * Verify duplicate position broadcasts inside the dedup window are dropped.
 * Important because this is the primary airtime-saving behavior.
 */
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

/**
 * Verify changed coordinates are forwarded even with dedup enabled.
 * Important so real movement updates are never suppressed as duplicates.
 */
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

/**
 * Verify rate limiting drops only after exceeding the configured threshold.
 * Important to protect threshold semantics from off-by-one regressions.
 */
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

/**
 * Verify routing/admin traffic is exempt from rate limiting.
 * Important because throttling control traffic can destabilize the mesh.
 */
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

/**
 * Verify packets sourced from this node bypass dedup and rate limiting.
 * Important so local transmissions are not accidentally self-throttled.
 */
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

/**
 * Verify router role clamps NodeInfo response hops to router-safe maximum.
 * Important so large config values cannot widen response scope unexpectedly.
 */
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

/**
 * Verify NodeInfo direct-response success path and reply packet fields.
 * Important because this path consumes the request and generates a spoofed cached reply.
 */
static void test_tm_nodeinfo_directResponse_respondsFromCache(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response = true;
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    mockNodeDB->setCachedNode(kTargetNode);

    MockRouter mockRouter;
    mockRouter.addInterface(std::unique_ptr<RadioInterface>(new MockRadioInterface()));
    MeshService mockService;
    router = &mockRouter;
    service = &mockService;

    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, kRemoteNode, kTargetNode);
    request.decoded.want_response = true;
    request.id = 0x13572468;
    request.hop_start = 3;
    request.hop_limit = 3; // direct request (0 hops away)

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(result));
    TEST_ASSERT_TRUE(module.ignoreRequestFlag());
    TEST_ASSERT_EQUAL_UINT32(1, stats.nodeinfo_cache_hits);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(mockRouter.sentPackets.size()));

    const meshtastic_MeshPacket &reply = mockRouter.sentPackets.front();
    TEST_ASSERT_EQUAL_INT(meshtastic_PortNum_NODEINFO_APP, reply.decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(kTargetNode, reply.from);
    TEST_ASSERT_EQUAL_UINT32(kRemoteNode, reply.to);
    TEST_ASSERT_EQUAL_UINT32(request.id, reply.decoded.request_id);
    TEST_ASSERT_FALSE(reply.decoded.want_response);
    TEST_ASSERT_EQUAL_UINT8(0, reply.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(0, reply.hop_start);
    TEST_ASSERT_EQUAL_UINT8(mockNodeDB->getLastByteOfNodeNum(kRemoteNode), reply.next_hop);
}

/**
 * Verify client role only answers direct (0-hop) NodeInfo requests.
 * Important so clients do not answer relayed requests outside intended scope.
 */
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

/**
 * Verify relayed telemetry broadcasts are hop-exhausted when enabled.
 * Important to prevent further mesh propagation while still allowing one relay step.
 */
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

/**
 * Verify hop exhaustion skips unicast and local-origin packets.
 * Important to avoid mutating traffic that should retain normal forwarding behavior.
 */
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

/**
 * Verify position dedup window expires and later duplicates are allowed.
 * Important so periodic identical reports can resume after cooldown.
 */
static void test_tm_positionDedup_allowsDuplicateAfterIntervalExpires(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 16;
    moduleConfig.traffic_management.position_min_interval_secs = 1;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket third = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    testDelay(1200);
    ProcessMessage r3 = module.handleReceived(third);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify interval=0 disables position deduplication.
 * Important because this is an explicit configuration escape hatch.
 */
static void test_tm_positionDedup_intervalZero_neverDrops(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 16;
    moduleConfig.traffic_management.position_min_interval_secs = 0;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
}

/**
 * Verify precision values above 32 are clamped safely.
 * Important to keep dedup behavior deterministic under invalid config input.
 */
static void test_tm_positionDedup_precisionAbove32_clamps(void)
{
    moduleConfig.traffic_management.position_dedup_enabled = true;
    moduleConfig.traffic_management.position_precision_bits = 99;
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify rate-limit counters reset after the window expires.
 * Important so temporary bursts do not cause persistent throttling.
 */
static void test_tm_rateLimit_resetsAfterWindowExpires(void)
{
    moduleConfig.traffic_management.rate_limit_enabled = true;
    moduleConfig.traffic_management.rate_limit_window_secs = 1;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    testDelay(1200);
    ProcessMessage r3 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rate_limit_drops);
}

/**
 * Verify rate-limit thresholds above 255 effectively clamp to 255.
 * Important because counters are uint8_t and must not overflow behavior.
 */
static void test_tm_rateLimit_thresholdAbove255_clamps(void)
{
    moduleConfig.traffic_management.rate_limit_enabled = true;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 300;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);

    for (int i = 0; i < 255; i++) {
        ProcessMessage result = module.handleReceived(packet);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    }
    ProcessMessage dropped = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(dropped));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rate_limit_drops);
}

/**
 * Verify unknown-packet tracking resets after its active window expires.
 * Important so old unknown traffic does not trigger delayed drops.
 */
static void test_tm_unknownPackets_resetAfterWindowExpires(void)
{
    moduleConfig.traffic_management.drop_unknown_enabled = true;
    moduleConfig.traffic_management.unknown_packet_threshold = 1;
    moduleConfig.traffic_management.rate_limit_window_secs = 1;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    testDelay(1200);
    ProcessMessage r3 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unknown_packet_drops);
}

/**
 * Verify unknown threshold values above 255 clamp to the counter ceiling.
 * Important to align config semantics with saturating counter storage.
 */
static void test_tm_unknownPackets_thresholdAbove255_clamps(void)
{
    moduleConfig.traffic_management.drop_unknown_enabled = true;
    moduleConfig.traffic_management.unknown_packet_threshold = 300;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode);

    for (int i = 0; i < 255; i++) {
        ProcessMessage result = module.handleReceived(packet);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    }
    ProcessMessage dropped = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(dropped));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unknown_packet_drops);
}

/**
 * Verify relayed position broadcasts can also be hop-exhausted.
 * Important because telemetry and position use separate exhaust flags.
 */
static void test_tm_alterReceived_exhaustsRelayedPositionBroadcast(void)
{
    moduleConfig.traffic_management.exhaust_hop_position = true;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makePositionPacket(kRemoteNode, 374221234, -1220845678, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 2;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(0, packet.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(4, packet.hop_start);
    TEST_ASSERT_TRUE(module.shouldExhaustHops());
    TEST_ASSERT_EQUAL_UINT32(1, stats.hop_exhausted_packets);
}

/**
 * Verify hop exhaustion ignores undecoded/encrypted packets.
 * Important so we never mutate packets that were not decoded by this module.
 */
static void test_tm_alterReceived_skipsUndecodedPackets(void)
{
    moduleConfig.traffic_management.exhaust_hop_telemetry = true;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 3;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(5, packet.hop_start);
    TEST_ASSERT_EQUAL_UINT8(3, packet.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops());
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

/**
 * Verify exhaustRequested is per-packet and resets on next handleReceived().
 * Important so a prior packet cannot leak hop-exhaust state into later packets.
 */
static void test_tm_alterReceived_resetExhaustFlagOnNextPacket(void)
{
    moduleConfig.traffic_management.exhaust_hop_telemetry = true;
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket telemetry = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, NODENUM_BROADCAST);
    telemetry.hop_start = 5;
    telemetry.hop_limit = 3;
    module.alterReceived(telemetry);
    TEST_ASSERT_TRUE(module.shouldExhaustHops());

    meshtastic_MeshPacket text = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);
    ProcessMessage result = module.handleReceived(text);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_FALSE(module.shouldExhaustHops());
    TEST_ASSERT_EQUAL_UINT32(1, stats.hop_exhausted_packets);
}

/**
 * Verify runOnce() returns sleep-forever interval when module is disabled.
 * Important to ensure the maintenance thread is effectively inert when off.
 */
static void test_tm_runOnce_disabledReturnsMaxInterval(void)
{
    moduleConfig.traffic_management.enabled = false;
    TrafficManagementModuleTestShim module;

    int32_t interval = module.runOnce();

    TEST_ASSERT_EQUAL_INT32(INT32_MAX, interval);
}

/**
 * Verify runOnce() returns the maintenance cadence when enabled.
 * Important so periodic cache housekeeping continues at expected interval.
 */
static void test_tm_runOnce_enabledReturnsMaintenanceInterval(void)
{
    TrafficManagementModuleTestShim module;

    int32_t interval = module.runOnce();

    TEST_ASSERT_EQUAL_INT32(60 * 1000, interval);
}

} // namespace

void setUp(void)
{
    resetTrafficConfig();
}
void tearDown(void) {}

void setup()
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
    RUN_TEST(test_tm_nodeinfo_directResponse_respondsFromCache);
    RUN_TEST(test_tm_nodeinfo_clientClamp_skipsWhenNotDirect);
    RUN_TEST(test_tm_alterReceived_exhaustsRelayedTelemetryBroadcast);
    RUN_TEST(test_tm_alterReceived_skipsLocalAndUnicast);
    RUN_TEST(test_tm_positionDedup_allowsDuplicateAfterIntervalExpires);
    RUN_TEST(test_tm_positionDedup_intervalZero_neverDrops);
    RUN_TEST(test_tm_positionDedup_precisionAbove32_clamps);
    RUN_TEST(test_tm_rateLimit_resetsAfterWindowExpires);
    RUN_TEST(test_tm_rateLimit_thresholdAbove255_clamps);
    RUN_TEST(test_tm_unknownPackets_resetAfterWindowExpires);
    RUN_TEST(test_tm_unknownPackets_thresholdAbove255_clamps);
    RUN_TEST(test_tm_alterReceived_exhaustsRelayedPositionBroadcast);
    RUN_TEST(test_tm_alterReceived_skipsUndecodedPackets);
    RUN_TEST(test_tm_alterReceived_resetExhaustFlagOnNextPacket);
    RUN_TEST(test_tm_runOnce_disabledReturnsMaxInterval);
    RUN_TEST(test_tm_runOnce_enabledReturnsMaintenanceInterval);
    exit(UNITY_END());
}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif

void loop() {}
