#include "MeshTypes.h" // Include BEFORE TestUtil.h - provides HAS_TRAFFIC_MANAGEMENT (via mesh-pb-constants.h)
#include "TestUtil.h"
#include <cstdlib>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define TM_TEST_ENTRY extern "C"
#else
#define TM_TEST_ENTRY
#endif

#if HAS_TRAFFIC_MANAGEMENT

#include "airtime.h"
#include "mesh/CryptoEngine.h"
#include "mesh/Default.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/TrafficManagementModule.h"
#include "support/DeterministicRng.h" // rngSeed/rngNext/rngRange - shared seeded LCG
#include <climits>
#include <cstring>
#include <memory>
#include <pb_encode.h>
#include <vector>

namespace
{

constexpr NodeNum kLocalNode = 0x11111111;
constexpr NodeNum kRemoteNode = 0x22222222;
constexpr NodeNum kTargetNode = 0x33333333;

// Telemetry hop exhaustion is gated on channel congestion (alterReceived checks
// airTime->isTxAllowedChannelUtil/isTxAllowedAirUtil). Installs a global
// airTime reporting 100% channel utilization for the enclosing scope.
class ScopedBusyAirTime
{
  public:
    ScopedBusyAirTime() : previous(airTime)
    {
        for (uint32_t i = 0; i < CHANNEL_UTILIZATION_PERIODS; i++)
            busy.channelUtilization[i] = 10000; // 10 s of airtime per 10 s period
        airTime = &busy;
    }
    ~ScopedBusyAirTime() { airTime = previous; }

  private:
    AirTime busy;
    AirTime *previous;
};

class MockNodeDB : public NodeDB
{
  public:
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) override
    {
        if (hasCachedNode && n == cachedNodeNum)
            return &cachedNode;
        return NodeDB::getMeshNode(n);
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
        cachedNode.bitfield |= NODEINFO_BITFIELD_HAS_USER_MASK;
    }

    // Role the TMM should see for the cached node (sender-role-aware throttles).
    void setCachedNodeRole(meshtastic_Config_DeviceConfig_Role role) { cachedNode.role = role; }

    // Direct mutable access to the cached node for fine-grained bitfield manipulation in tests.
    meshtastic_NodeInfoLite &cachedNodeForTest()
    {
        hasCachedNode = true;
        return cachedNode;
    }

    // Seed a node into the hot-store buffer at index 1 (index 0 is reserved for
    // "self"). Respects the fixed-buffer invariant: `meshNodes` is a buffer of
    // MAX_NUM_NODES slots with `numMeshNodes` as the logical count - we grow the
    // buffer if needed and bump the count, never clear()/push_back() (which would
    // shrink it and break NodeDB::resetNodes()'s begin()+1..end() fill).
    void setHotNode(NodeNum n, uint8_t nextHop)
    {
        if (meshNodes->size() < 2)
            meshNodes->resize(2);
        (*meshNodes)[1] = meshtastic_NodeInfoLite_init_zero;
        (*meshNodes)[1].num = n;
        (*meshNodes)[1].next_hop = nextHop;
        numMeshNodes = 2;
    }

    // Evict everything but "self" - simulates the hot DB rolling over. Logical
    // count only; the buffer is left intact so the invariant holds.
    void rollHotStore()
    {
        numMeshNodes = 1;
        clearCachedNode();
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
    using TrafficManagementModule::flushCache;
    using TrafficManagementModule::handleReceived;
    using TrafficManagementModule::peekCachedRole;
    using TrafficManagementModule::runOnce;

    bool ignoreRequestFlag() const { return ignoreRequest; }
};

MockNodeDB *mockNodeDB = nullptr;

static void resetTrafficConfig()
{
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    moduleConfig.has_traffic_management = true;
    moduleConfig.traffic_management = meshtastic_ModuleConfig_TrafficManagementConfig_init_zero;

    config = meshtastic_LocalConfig_init_zero;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    channelFile = meshtastic_ChannelFile_init_zero;
    owner.is_licensed = false;

    myNodeInfo.my_node_num = kLocalNode;

    router = nullptr;
    service = nullptr;

    mockNodeDB->resetNodes();
    mockNodeDB->clearCachedNode();
    nodeDB = mockNodeDB;

    // Virtual clock base (1 h in, so tick subtraction never underflows). Tests advance time by
    // bumping TrafficManagementModule::s_testNowMs instead of sleeping real seconds across a tick.
    TrafficManagementModule::s_testNowMs = 3600000;
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

static meshtastic_MeshPacket makePositionPacketWithPrecision(NodeNum from, int32_t lat, int32_t lon, uint32_t precisionBits)
{
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, from, NODENUM_BROADCAST);
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.has_longitude_i = true;
    pos.latitude_i = lat;
    pos.longitude_i = lon;
    pos.precision_bits = precisionBits;

    packet.decoded.payload.size =
        pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes), &meshtastic_Position_msg, &pos);
    return packet;
}

static bool decodePositionPayload(const meshtastic_MeshPacket &packet, meshtastic_Position &out)
{
    out = meshtastic_Position_init_zero;
    return pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_Position_msg, &out);
}

// Primary channel with a well-known single-byte PSK and the (empty -> preset)
// default name, so Channels::isWellKnownChannel(0) is true.
static void installWellKnownPrimaryChannel()
{
    channelFile = meshtastic_ChannelFile_init_zero;
    channelFile.channels_count = 1;
    channelFile.channels[0].index = 0;
    channelFile.channels[0].has_settings = true;
    channelFile.channels[0].role = meshtastic_Channel_Role_PRIMARY;
    channelFile.channels[0].settings.psk.size = 1;
    channelFile.channels[0].settings.psk.bytes[0] = 1;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
}

// Install the well-known primary channel AND set a specific position_precision so
// shouldDropPosition() uses that precision ceiling rather than the default fallback.
// precision=0 means "no channel ceiling" and falls back to the firmware default (19 bits).
static void installWellKnownPrimaryChannelWithPrecision(uint32_t precision)
{
    installWellKnownPrimaryChannel();
    channelFile.channels[0].settings.has_module_settings = true;
    channelFile.channels[0].settings.module_settings.position_precision = precision;
}

static meshtastic_MeshPacket makeNodeInfoPacket(NodeNum from, const char *longName, const char *shortName)
{
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, from, NODENUM_BROADCAST);

    meshtastic_User user = meshtastic_User_init_zero;
    snprintf(user.id, sizeof(user.id), "!%08x", from);
    strncpy(user.long_name, longName, sizeof(user.long_name) - 1);
    strncpy(user.short_name, shortName, sizeof(user.short_name) - 1);

    packet.decoded.payload.size =
        pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes), &meshtastic_User_msg, &user);
    return packet;
}

static meshtastic_MeshPacket makeNodeInfoPacketWithRole(NodeNum from, meshtastic_Config_DeviceConfig_Role role)
{
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, from, NODENUM_BROADCAST);

    meshtastic_User user = meshtastic_User_init_zero;
    snprintf(user.id, sizeof(user.id), "!%08x", from);
    strncpy(user.long_name, "rolenode", sizeof(user.long_name) - 1);
    strncpy(user.short_name, "rn", sizeof(user.short_name) - 1);
    user.role = role;

    packet.decoded.payload.size =
        pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes), &meshtastic_User_msg, &user);
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
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);
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
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);
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
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 3;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode);

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
 * Verify packets sourced from this node bypass dedup and rate limiting.
 * Important so local transmissions are not accidentally self-throttled.
 */
static void test_tm_fromUs_bypassesPositionAndRateFilters(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    installWellKnownPrimaryChannelWithPrecision(16);
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
 * Verify locally addressed packets are never dropped by transit shaping.
 * Important so dedup/rate limiting do not suppress end-user delivery.
 */
static void test_tm_localDestination_bypassesTransitFilters(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    installWellKnownPrimaryChannelWithPrecision(16);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket position1 = makePositionPacket(kRemoteNode, 374221234, -1220845678, kLocalNode);
    meshtastic_MeshPacket position2 = makePositionPacket(kRemoteNode, 374221234, -1220845678, kLocalNode);
    meshtastic_MeshPacket text1 = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kLocalNode);
    meshtastic_MeshPacket text2 = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kLocalNode);

    ProcessMessage p1 = module.handleReceived(position1);
    ProcessMessage p2 = module.handleReceived(position2);
    ProcessMessage t1 = module.handleReceived(text1);
    ProcessMessage t2 = module.handleReceived(text2);
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
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.config_ok_to_mqtt = true;
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
    TEST_ASSERT_TRUE(reply.decoded.has_bitfield);
    TEST_ASSERT_EQUAL_UINT8(BITFIELD_OK_TO_MQTT_MASK, reply.decoded.bitfield);
}

/**
 * Verify cached direct replies still preserve requester NodeInfo learning.
 * Important so consuming the request does not skip NodeDB refresh for observers.
 */
static void test_tm_nodeinfo_directResponse_learnsRequestorNodeInfo(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    mockNodeDB->setCachedNode(kTargetNode);

    MockRouter mockRouter;
    mockRouter.addInterface(std::unique_ptr<RadioInterface>(new MockRadioInterface()));
    MeshService mockService;
    router = &mockRouter;
    service = &mockService;

    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket request = makeNodeInfoPacket(kRemoteNode, "requester-long", "rq");
    request.to = kTargetNode;
    request.decoded.want_response = true;
    request.id = 0x01020304;
    request.hop_start = 3;
    request.hop_limit = 3;

    ProcessMessage result = module.handleReceived(request);
    meshtastic_NodeInfoLite *requestor = mockNodeDB->getMeshNode(kRemoteNode);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(result));
    TEST_ASSERT_NOT_NULL(requestor);
    TEST_ASSERT_TRUE((requestor->bitfield & NODEINFO_BITFIELD_HAS_USER_MASK) != 0);
    TEST_ASSERT_EQUAL_STRING("requester-long", requestor->long_name);
    TEST_ASSERT_EQUAL_STRING("rq", requestor->short_name);
    TEST_ASSERT_EQUAL_UINT8(request.channel, requestor->channel);
}

/**
 * Verify client role only answers direct (0-hop) NodeInfo requests.
 * Important so clients do not answer relayed requests outside intended scope.
 */
static void test_tm_nodeinfo_clientClamp_skipsWhenNotDirect(void)
{
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

#if !(defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM))
/**
 * Verify non-PSRAM builds require NodeDB for direct NodeInfo responses.
 * Important because fallback should only happen through node-wide data when
 * the dedicated PSRAM cache does not exist.
 */
static void test_tm_nodeinfo_directResponse_withoutNodeDbEntry_skips(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    mockNodeDB->clearCachedNode();

    MockRouter mockRouter;
    mockRouter.addInterface(std::unique_ptr<RadioInterface>(new MockRadioInterface()));
    MeshService mockService;
    router = &mockRouter;
    service = &mockService;

    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, kRemoteNode, kTargetNode);
    request.decoded.want_response = true;
    request.hop_start = 3;
    request.hop_limit = 3;

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_FALSE(module.ignoreRequestFlag());
    TEST_ASSERT_EQUAL_UINT32(0, stats.nodeinfo_cache_hits);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(mockRouter.sentPackets.size()));
}
#endif

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
/**
 * Verify PSRAM NodeInfo cache can answer requests without NodeDB and that
 * shouldRespondToNodeInfo() uses cached bitfield metadata.
 */
static void test_tm_nodeinfo_directResponse_psramCacheRespondsAndPreservesBitfield(void)
{
    moduleConfig.traffic_management.nodeinfo_direct_response_max_hops = 10;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.config_ok_to_mqtt = true;
    mockNodeDB->clearCachedNode();

    MockRouter mockRouter;
    mockRouter.addInterface(std::unique_ptr<RadioInterface>(new MockRadioInterface()));
    MeshService mockService;
    router = &mockRouter;
    service = &mockService;

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket observed = makeNodeInfoPacket(kTargetNode, "target-long", "tg");
    observed.decoded.has_bitfield = true;
    observed.decoded.bitfield = BITFIELD_WANT_RESPONSE_MASK;
    observed.channel = 2;
    observed.rx_time = 123456;

    ProcessMessage observedResult = module.handleReceived(observed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(observedResult));

    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_NODEINFO_APP, kRemoteNode, kTargetNode);
    request.decoded.want_response = true;
    request.id = 0x24681357;
    request.channel = 1;
    request.hop_start = 3;
    request.hop_limit = 3;

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(result));
    TEST_ASSERT_TRUE(module.ignoreRequestFlag());
    TEST_ASSERT_EQUAL_UINT32(1, stats.nodeinfo_cache_hits);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(mockRouter.sentPackets.size()));

    const meshtastic_MeshPacket &reply = mockRouter.sentPackets.front();
    TEST_ASSERT_TRUE(reply.decoded.has_bitfield);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BITFIELD_WANT_RESPONSE_MASK | BITFIELD_OK_TO_MQTT_MASK), reply.decoded.bitfield);
    TEST_ASSERT_EQUAL_UINT32(kTargetNode, reply.from);
    TEST_ASSERT_EQUAL_UINT32(kRemoteNode, reply.to);
    TEST_ASSERT_EQUAL_UINT8(request.channel, reply.channel);
    TEST_ASSERT_EQUAL_UINT32(request.id, reply.decoded.request_id);
}

/**
 * Verify PSRAM cache misses do not fall back to NodeDB.
 * Important so the dedicated PSRAM index stays logically separate from
 * NodeInfoModule/NodeDB when PSRAM is available.
 */
static void test_tm_nodeinfo_directResponse_psramMissDoesNotFallbackToNodeDb(void)
{
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
    request.hop_start = 3;
    request.hop_limit = 3;

    ProcessMessage result = module.handleReceived(request);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_FALSE(module.ignoreRequestFlag());
    TEST_ASSERT_EQUAL_UINT32(0, stats.nodeinfo_cache_hits);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(mockRouter.sentPackets.size()));
}
#endif

/**
 * Verify relayed telemetry broadcasts are NOT hop-exhausted.
 * exhaust_hop_telemetry / exhaust_hop_position have been removed from the config
 * as "not suitable right now" - alterReceived must leave hop_limit unchanged.
 */
static void test_tm_alterReceived_telemetryBroadcast_hopLimitUnchanged(void)
{
    ScopedBusyAirTime busyChannel; // congestion present but exhaust is disabled
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 3;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(3, packet.hop_limit); // unchanged
    TEST_ASSERT_EQUAL_UINT8(5, packet.hop_start); // unchanged
    TEST_ASSERT_FALSE(module.shouldExhaustHops(packet));
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

/**
 * Verify alterReceived does not modify unicast or local-origin packets.
 * The precision clamp (the only active alterReceived path) only fires for
 * broadcast position packets from remote nodes - these should be untouched.
 */
static void test_tm_alterReceived_skipsLocalAndUnicast(void)
{
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket unicast = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kTargetNode);
    unicast.hop_start = 5;
    unicast.hop_limit = 3;
    module.alterReceived(unicast);
    TEST_ASSERT_EQUAL_UINT8(3, unicast.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops(unicast));

    meshtastic_MeshPacket fromUs = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kLocalNode, NODENUM_BROADCAST);
    fromUs.hop_start = 5;
    fromUs.hop_limit = 3;
    module.alterReceived(fromUs);
    TEST_ASSERT_EQUAL_UINT8(3, fromUs.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops(fromUs));

    meshtastic_TrafficManagementStats stats = module.getStats();
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

/**
 * Verify position dedup window expires and later duplicates are allowed.
 * Important so periodic identical reports can resume after cooldown.
 */
static void test_tm_positionDedup_allowsDuplicateAfterIntervalExpires(void)
{
    // 360 s = 1 pos-tick (kPosTimeTickMs); advance the virtual clock past one tick period.
    moduleConfig.traffic_management.position_min_interval_secs = 360;
    installWellKnownPrimaryChannelWithPrecision(16);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket third = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    TrafficManagementModule::s_testNowMs += 360001; // advance past one 6-min pos-tick (virtual clock)
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
    // position_min_interval_secs=0 disables the drop gate (shouldDropPosition returns false for any packet).
    moduleConfig.traffic_management.position_min_interval_secs = 0;
    installWellKnownPrimaryChannelWithPrecision(16);
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
 * Verify precision values above 32 fall back to default precision.
 * Important so invalid config uses the documented default behavior.
 */
static void test_tm_positionDedup_precisionAbove32_usesDefaultPrecision(void)
{
    // Channel precision=99 is out of range; sanitizePositionPrecision falls back to default (19 bits).
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(99);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 384221234, -1210845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
}

/**
 * Verify the dedup fingerprint does not collapse positions that are distinct at the
 * channel's *effective* precision. Dedup only runs on well-known (public) channels,
 * where precision is capped at MAX_POSITION_PRECISION_PUBLIC_KEY (15) regardless of the
 * channel's configured value - so the requested 32 is clamped to 15. Positions must
 * therefore differ in the top 15 bits (>= 2^17 raw units) to read as distinct; here
 * they differ by 2^18, well clear of the precision-15 grid, so neither is dropped.
 */
static void test_tm_positionDedup_distinctAtClampedChannelPrecision(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(32); // clamped to 15 on a public channel
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 374221234 + (1 << 18), -1220845678 + (1 << 18));

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
}

/**
 * Verify channel precision=0 (no channel ceiling set) falls back to the firmware
 * default precision (19 bits / ~90 m cells). Positions more than one default grid
 * cell apart must remain distinct, not collapse into one fingerprint.
 */
static void test_tm_positionDedup_precisionZero_channelFallsBackToDefault(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    // precision=0 in the channel → getPositionPrecisionForChannel returns 0 → falls back to default 19 bits.
    installWellKnownPrimaryChannelWithPrecision(0);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket second = makePositionPacket(kRemoteNode, 384221234, -1210845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(second);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(0, stats.position_dedup_drops);
}

/**
 * Verify epoch reset invalidates stale position identity for dedup.
 * Important so reset paths cannot leak prior packet identity into new windows.
 */
static void test_tm_positionDedup_cacheFlush_doesNotDropFirstPacketAfterFlush(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterFlush = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket duplicate = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    module.flushCache();
    ProcessMessage r2 = module.handleReceived(afterFlush);
    ProcessMessage r3 = module.handleReceived(duplicate);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify non-position cache state does not make the first fingerprint-0 position look duplicated.
 * Important so unified cache entries from other features cannot leak into dedup decisions.
 */
static void test_tm_positionDedup_priorRateState_doesNotDropFirstFingerprintZero(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 10;
    installWellKnownPrimaryChannelWithPrecision(16);
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket telemetry = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode);
    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 0x12300000, 0x45600000);
    meshtastic_MeshPacket duplicate = makePositionPacket(kRemoteNode, 0x12300000, 0x45600000);

    ProcessMessage seeded = module.handleReceived(telemetry);
    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(duplicate);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(seeded));
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
    // 300 s = 1 rate-tick (kRateTimeTickMs); advance the virtual clock past one tick period.
    moduleConfig.traffic_management.rate_limit_window_secs = 300;
    moduleConfig.traffic_management.rate_limit_max_packets = 1;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    TrafficManagementModule::s_testNowMs += 300001; // advance past one 5-min rate-tick (virtual clock)
    ProcessMessage r3 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rate_limit_drops);
}
/**
 * Verify unknown-packet tracking resets after its active window expires.
 * Important so old unknown traffic does not trigger delayed drops.
 */
static void test_tm_unknownPackets_resetAfterWindowExpires(void)
{
    moduleConfig.traffic_management.unknown_packet_threshold = 1;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode);

    ProcessMessage r1 = module.handleReceived(packet);
    ProcessMessage r2 = module.handleReceived(packet);
    TrafficManagementModule::s_testNowMs += 300001; // advance past 5 unknown-ticks (5 × 60s) (virtual clock)
    ProcessMessage r3 = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unknown_packet_drops);
}

/**
 * Verify unknown threshold values above the implementation cap (60) are clamped.
 * The counter is a 6-bit field (saturates at 63); threshold is capped at 60 so a
 * saturated reading always exceeds it. A config value of 300 should behave as 60.
 */
static void test_tm_unknownPackets_thresholdAbove255_clamps(void)
{
    moduleConfig.traffic_management.unknown_packet_threshold = 300;
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode);

    for (int i = 0; i < 60; i++) {
        ProcessMessage result = module.handleReceived(packet);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    }
    ProcessMessage dropped = module.handleReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(dropped));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unknown_packet_drops);
}

/**
 * Verify relayed position broadcasts are NOT hop-exhausted.
 * exhaust_hop_position has been removed - alterReceived must leave hop_limit unchanged.
 */
static void test_tm_alterReceived_positionBroadcast_hopLimitUnchanged(void)
{
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makePositionPacket(kRemoteNode, 374221234, -1220845678, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 2;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(2, packet.hop_limit); // unchanged
    TEST_ASSERT_EQUAL_UINT8(5, packet.hop_start); // unchanged
    TEST_ASSERT_FALSE(module.shouldExhaustHops(packet));
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}
/**
 * Verify alterReceived ignores undecoded/encrypted packets.
 * Important so we never mutate packets that were not decoded by this module.
 */
static void test_tm_alterReceived_skipsUndecodedPackets(void)
{
    TrafficManagementModuleTestShim module;
    meshtastic_MeshPacket packet = makeUnknownPacket(kRemoteNode, NODENUM_BROADCAST);
    packet.hop_start = 5;
    packet.hop_limit = 3;

    module.alterReceived(packet);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_UINT8(5, packet.hop_start);
    TEST_ASSERT_EQUAL_UINT8(3, packet.hop_limit);
    TEST_ASSERT_FALSE(module.shouldExhaustHops(packet));
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

/**
 * Verify shouldExhaustHops() always returns false - exhaust_hop_* features are
 * removed, so the exhaustRequested flag is never set.
 * Guards against accidental re-enablement without updating the flag logic.
 */
static void test_tm_alterReceived_exhaustFlagAlwaysFalse(void)
{
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket telemetry = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, NODENUM_BROADCAST);
    telemetry.hop_start = 5;
    telemetry.hop_limit = 3;
    module.alterReceived(telemetry);
    TEST_ASSERT_FALSE(module.shouldExhaustHops(telemetry));

    meshtastic_MeshPacket text = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode);
    ProcessMessage result = module.handleReceived(text);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(result));
    TEST_ASSERT_FALSE(module.shouldExhaustHops(telemetry));
    TEST_ASSERT_EQUAL_UINT32(0, stats.hop_exhausted_packets);
}

/**
 * Verify shouldExhaustHops() returns false for any packet regardless of from/id.
 * Since exhaust is removed, the from+id scope check is moot - this guards that
 * the always-false invariant holds across multiple distinct packets.
 */
static void test_tm_alterReceived_exhaustFlag_isPacketScoped(void)
{
    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket p1 = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, NODENUM_BROADCAST);
    p1.id = 0x1010;
    p1.hop_start = 5;
    p1.hop_limit = 3;
    module.alterReceived(p1);

    meshtastic_MeshPacket p2 = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kTargetNode, NODENUM_BROADCAST);
    p2.id = 0x2020;
    p2.hop_start = 4;
    p2.hop_limit = 0;

    TEST_ASSERT_FALSE(module.shouldExhaustHops(p1));
    TEST_ASSERT_FALSE(module.shouldExhaustHops(p2));
}

/**
 * Verify runOnce() returns sleep-forever interval when has_traffic_management is false.
 * TMM has no runtime enable flag - the presence bit is the only runtime gate.
 */
static void test_tm_runOnce_disabledReturnsMaxInterval(void)
{
    moduleConfig.has_traffic_management = false;
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

// ---------------------------------------------------------------------------
// Next-hop overflow cache
// ---------------------------------------------------------------------------

/**
 * Round-trip set/get of a confirmed next hop, plus the input guards.
 */
static void test_tm_nextHop_setAndGetRoundTrip(void)
{
    TrafficManagementModuleTestShim module;

    // Unknown node yields no hint.
    TEST_ASSERT_EQUAL_UINT8(0, module.getNextHopHint(kTargetNode));

    // Store a confirmed hop and read it back.
    module.setNextHop(kTargetNode, 0x42);
    TEST_ASSERT_EQUAL_UINT8(0x42, module.getNextHopHint(kTargetNode));

    // Zero dest and zero byte are rejected (no spurious entry created).
    module.setNextHop(0, 0x42);
    module.setNextHop(kRemoteNode, 0);
    TEST_ASSERT_EQUAL_UINT8(0, module.getNextHopHint(kRemoteNode));

    // Last-write-wins on re-confirmation.
    module.setNextHop(kTargetNode, 0x99);
    TEST_ASSERT_EQUAL_UINT8(0x99, module.getNextHopHint(kTargetNode));
}

/**
 * The headline scenario: a node carrying a next hop in the hot NodeInfoLite DB
 * is warm-loaded into the TMM cache, then the hot DB is "rolled" (the node ages
 * out entirely). The hint must still be served - now exclusively from TMM.
 */
static void test_tm_nextHop_servedAfterNodeDbRoll(void)
{
    TrafficManagementModuleTestShim module;

    // Seed the hot NodeInfoLite DB with a node that has a confirmed next hop.
    mockNodeDB->setHotNode(kTargetNode, 0x42);

    // Warm-start the overflow cache from the hot DB.
    module.preloadNextHopsFromNodeDB();
    TEST_ASSERT_EQUAL_UINT8(0x42, module.getNextHopHint(kTargetNode));

    // Roll the main NodeInfoLite DB: the node is evicted from the hot store.
    mockNodeDB->rollHotStore();
    TEST_ASSERT_NULL(nodeDB->getMeshNode(kTargetNode)); // gone from the hot store

    // Hit is still served - proving it now comes from the TMM overflow cache.
    TEST_ASSERT_EQUAL_UINT8(0x42, module.getNextHopHint(kTargetNode));
}

/**
 * Preload must not clobber a freshly-learned (confirmed) hop with a possibly
 * stale persisted one from NodeInfoLite.
 */
static void test_tm_nextHop_preloadDoesNotClobberLearned(void)
{
    TrafficManagementModuleTestShim module;

    // A fresher confirmed hop is already cached.
    module.setNextHop(kTargetNode, 0x99);

    // The hot DB carries an older next hop for the same node.
    mockNodeDB->setHotNode(kTargetNode, 0x42);

    module.preloadNextHopsFromNodeDB();

    // The freshly-learned hop survives.
    TEST_ASSERT_EQUAL_UINT8(0x99, module.getNextHopHint(kTargetNode));
}

/**
 * A pure routing hint (no dedup/rate/unknown state) must survive the maintenance
 * sweep - next_hop != 0 keeps the slot alive even though it has no TTL.
 */
static void test_tm_nextHop_keptAliveAcrossMaintenanceSweep(void)
{
    TrafficManagementModuleTestShim module;

    module.setNextHop(kTargetNode, 0x42);

    // The sweep frees slots whose sub-stores are all empty; next_hop must veto that.
    module.runOnce();

    TEST_ASSERT_EQUAL_UINT8(0x42, module.getNextHopHint(kTargetNode));
}

// ---------------------------------------------------------------------------
// Role exceptions: TRACKER and LOST_AND_FOUND
// ---------------------------------------------------------------------------

/**
 * Verify TRACKER role caps the dedup window at 1 hour.
 * A duplicate position that would normally be blocked for 11 h (default) must
 * be forwarded once the 1-hour tracker cap expires.
 */
static void test_tm_trackerRole_capsDedupWindowAtOneHour(void)
{
    // Operator interval is 11 h - longer than the tracker cap.
    moduleConfig.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterCap = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup); // still within 1-hour cap
    // Advance past 1 hour (tracker cap = 3600 s; pos-tick = 360 s → 10 ticks).
    TrafficManagementModule::s_testNowMs += (default_traffic_mgmt_tracker_position_min_interval_secs * 1000UL) + 1;
    ProcessMessage r3 = module.handleReceived(afterCap);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify TAK_TRACKER role receives the same 1-hour dedup cap as TRACKER.
 * Both roles share the same exception branch; this guards against future divergence.
 */
static void test_tm_takTrackerRole_capsDedupWindowAtOneHour(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TAK_TRACKER);

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterCap = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup);
    TrafficManagementModule::s_testNowMs += (default_traffic_mgmt_tracker_position_min_interval_secs * 1000UL) + 1;
    ProcessMessage r3 = module.handleReceived(afterCap);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify the tracker role exception survives the node being evicted from BOTH the
 * hot and warm NodeDB stores - the TMM unified cache is the third fallback. The
 * role is cached on the entry while NodeDB still knows the node; once NodeDB
 * forgets it (getNodeRole → CLIENT), the cached role must keep the 1-hour cap
 * applied instead of reverting to the 11-hour default interval.
 */
static void test_tm_trackerRole_survivesNodeDbEvictionViaCachedRole(void)
{
    // Operator interval is 11 h - longer than the tracker cap.
    moduleConfig.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterCap = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    // First packet: NodeDB knows the role; it is cached onto the TMM entry.
    ProcessMessage r1 = module.handleReceived(first);

    // Node ages out of NodeDB entirely (hot + warm). getNodeRole now returns CLIENT.
    mockNodeDB->clearCachedNode();

    ProcessMessage r2 = module.handleReceived(dup); // within 1-hour cap - still drop
    // Advance past the tracker cap (3600 s) but stay well under the 11-hour default.
    // Without the cached-role fallback this would still be inside the 11-hour window
    // (CLIENT → no exception) and wrongly drop; with it, the 1-hour cap lets it pass.
    TrafficManagementModule::s_testNowMs += (default_traffic_mgmt_tracker_position_min_interval_secs * 1000UL) + 1;
    ProcessMessage r3 = module.handleReceived(afterCap);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify a role change observed via NodeInfo updates the cached role (write-time update),
 * so an exception is dropped when a node is demoted from TRACKER back to CLIENT. The role
 * is read from the NodeInfo's User payload - the same event that updates NodeDB - not by
 * re-scanning NodeDB on the position path.
 */
static void test_tm_roleChange_viaNodeInfo_dropsTrackerException(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);

    TrafficManagementModuleTestShim module;

    // First position seeds the TMM entry with TRACKER (from NodeDB on isNew).
    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    ProcessMessage r1 = module.handleReceived(first);

    // The node demotes to CLIENT and announces it. The NodeInfo refresh must overwrite
    // the cached TRACKER role with CLIENT - even though the packet payload, not NodeDB,
    // is the source of truth here (NodeDB role left stale on purpose to prove the path).
    meshtastic_MeshPacket info = makeNodeInfoPacketWithRole(kRemoteNode, meshtastic_Config_DeviceConfig_Role_CLIENT);
    module.handleReceived(info);

    // Past the 1-hour tracker cap but within the 11-hour CLIENT interval. With the stale
    // TRACKER role this would pass; after the demotion it must drop (full interval applies).
    TrafficManagementModule::s_testNowMs += (default_traffic_mgmt_tracker_position_min_interval_secs * 1000UL) + 1;
    meshtastic_MeshPacket afterCap = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    ProcessMessage r2 = module.handleReceived(afterCap);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify a cached special (non-CLIENT) role pins the TMM entry through the maintenance
 * sweep: once the node's position state has expired and been cleared, the entry - and its
 * role - must still survive (role has no TTL), the same way a confirmed next-hop hint does.
 */
static void test_tm_specialRole_pinsEntryThroughSweep(void)
{
    // Short position interval → short pos TTL so the sweep clears pos state quickly.
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);

    TrafficManagementModuleTestShim module;

    module.handleReceived(makePositionPacket(kRemoteNode, 374221234, -1220845678));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(meshtastic_Config_DeviceConfig_Role_TRACKER), module.peekCachedRole(kRemoteNode));

    // Advance well past the position TTL, then sweep: pos state is cleared but the role
    // (no TTL) must keep the entry alive. Without the role pin the entry would be reclaimed
    // and peekCachedRole would return -1.
    TrafficManagementModule::s_testNowMs += 60UL * 60UL * 1000UL; // 1 h
    module.runOnce();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(meshtastic_Config_DeviceConfig_Role_TRACKER), module.peekCachedRole(kRemoteNode));
}

/**
 * Verify special-role entries are evicted last. A tracker that is the OLDEST entry must
 * survive cache pressure that evicts many newer CLIENT entries, because a cached
 * special role marks the entry "preferred" (like a next-hop hint) in victim selection.
 */
static void test_tm_specialRole_evictedLastUnderPressure(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);

    TrafficManagementModuleTestShim module;

    // Oldest entry: a tracker. Seed its role from NodeDB on first position.
    const NodeNum tracker = 0xAA0000FF;
    mockNodeDB->setCachedNode(tracker);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);
    module.handleReceived(makePositionPacket(tracker, 100, 200));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(meshtastic_Config_DeviceConfig_Role_TRACKER), module.peekCachedRole(tracker));

    mockNodeDB->clearCachedNode(); // every subsequent filler resolves to CLIENT (unprotected)

    // Fill past capacity with newer CLIENT nodes, forcing many evictions. The tracker is
    // the stalest entry but is "preferred", so an unprotected client must always be the
    // victim instead - the tracker must never be evicted.
    for (uint32_t i = 0; i < static_cast<uint32_t>(TRAFFIC_MANAGEMENT_CACHE_SIZE) + 50; i++) {
        const NodeNum filler = 0x01000000u + i;
        module.handleReceived(makePositionPacket(filler, 300 + static_cast<int>(i), 400 + static_cast<int>(i)));
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(meshtastic_Config_DeviceConfig_Role_TRACKER), module.peekCachedRole(tracker));
}

/**
 * Verify the tracker cap never lengthens an operator-configured interval shorter
 * than the cap default. The cap is a ceiling, not a floor.
 */
static void test_tm_trackerRole_doesNotLengthenShorterOperatorInterval(void)
{
    // Operator set 5-minute interval - shorter than the 1-hour tracker cap.
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_TRACKER);

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterShortInterval = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup);
    // The 300 s operator interval rounds to 1 pos-tick (360 s) - dedup is tick-granular.
    // Advance past one full tick to verify the window is 1 tick (not the 10-tick tracker cap).
    TrafficManagementModule::s_testNowMs += 360'000UL + 1;
    ProcessMessage r3 = module.handleReceived(afterShortInterval);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify LOST_AND_FOUND role caps duplicate-position dedup at ~15 min (2 pos-ticks),
 * not the old one-tick fast-announce. A configured 11-hour interval is shortened to the
 * 15-min cap; a duplicate one tick later still drops, but one past the 2-tick cap passes.
 */
static void test_tm_lostAndFoundRole_capsDedupAtFifteenMinutes(void)
{
    // Long interval that would normally suppress duplicates for 11 h.
    moduleConfig.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
    installWellKnownPrimaryChannelWithPrecision(16);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND);

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterOneTick = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterCap = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup); // same tick - drop
    // One pos-tick later: STILL within the 15-min (~2-tick) cap, unlike the old 1-tick exception.
    // (360 s = kPosTimeTickMs, kept as a literal because the constant is private to the module.)
    TrafficManagementModule::s_testNowMs += 360'000UL + 1;
    ProcessMessage r3 = module.handleReceived(afterOneTick); // still drop
    // Jump past the full cap (>= 2 ticks since the last packet): now it passes.
    TrafficManagementModule::s_testNowMs += 2 * 360'000UL;
    ProcessMessage r4 = module.handleReceived(afterCap);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r4));
    TEST_ASSERT_EQUAL_UINT32(2, stats.position_dedup_drops);
}

/**
 * Verify a node with unknown role (absent from NodeDB) is not granted any role
 * exception: the full configured interval applies, so a duplicate inside that
 * window is dropped exactly as for an ordinary CLIENT.
 */
static void test_tm_unknownRole_noDbEntry_appliesFullInterval(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);

    // No cached node - getMeshNode returns nullptr for kRemoteNode.
    mockNodeDB->clearCachedNode();

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket afterShort = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup); // within 300-s window - must drop
    // The 300 s operator interval rounds to 1 pos-tick (360 s) - dedup is tick-granular.
    // Advance past one full tick to confirm the packet passes without any tracker exception.
    TrafficManagementModule::s_testNowMs += 360'000UL + 1;
    ProcessMessage r3 = module.handleReceived(afterShort);
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r3));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify a node present in NodeDB but without user info (HAS_USER bit clear)
 * is not granted a role exception: it falls back to CLIENT and the full
 * configured interval applies.
 */
static void test_tm_unknownRole_noUserBit_appliesFullInterval(void)
{
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    installWellKnownPrimaryChannelWithPrecision(16);

    // Node is in NodeDB but the HAS_USER bit is NOT set - role must be ignored.
    mockNodeDB->setCachedNode(kRemoteNode);
    // Clear the HAS_USER bit that setCachedNode sets, leaving role at CLIENT default.
    mockNodeDB->cachedNodeForTest().bitfield &= ~NODEINFO_BITFIELD_HAS_USER_MASK;
    mockNodeDB->cachedNodeForTest().role = meshtastic_Config_DeviceConfig_Role_TRACKER;

    TrafficManagementModuleTestShim module;

    meshtastic_MeshPacket first = makePositionPacket(kRemoteNode, 374221234, -1220845678);
    meshtastic_MeshPacket dup = makePositionPacket(kRemoteNode, 374221234, -1220845678);

    ProcessMessage r1 = module.handleReceived(first);
    ProcessMessage r2 = module.handleReceived(dup); // within 300-s window - must drop
    meshtastic_TrafficManagementStats stats = module.getStats();

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::CONTINUE), static_cast<int>(r1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ProcessMessage::STOP), static_cast<int>(r2));
    TEST_ASSERT_EQUAL_UINT32(1, stats.position_dedup_drops);
}

/**
 * Verify a LOST_AND_FOUND origin now GETS the relayed precision clamp - the
 * anti-dox exemption was removed, so a relayed position more precise than the
 * channel setting is clamped down to the channel ceiling like any other node's.
 */
static void test_tm_lostAndFoundRole_getsAlterReceivedPrecisionClamp(void)
{
    // Set channel precision ceiling to 13 bits. Must be <= MAX_POSITION_PRECISION_PUBLIC_KEY
    // (15) - well-known channels have a public PSK (size==1), so getPositionPrecisionForChannel
    // clamps any value above 15 via usesPublicKey().
    installWellKnownPrimaryChannelWithPrecision(13);

    mockNodeDB->setCachedNode(kRemoteNode);
    mockNodeDB->setCachedNodeRole(meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND);

    TrafficManagementModuleTestShim module;

    // Full-precision packet - 32 bits - exceeds the channel cap.
    const uint32_t fullPrecision = 32;
    meshtastic_MeshPacket packet = makePositionPacketWithPrecision(kRemoteNode, 374221234, -1220845678, fullPrecision);
    packet.hop_start = 3;
    packet.hop_limit = 2; // relayed (hop_limit < hop_start) so clamp logic applies

    module.alterReceived(packet);

    meshtastic_Position out;
    TEST_ASSERT_TRUE(decodePositionPayload(packet, out));
    // Clamped to channel ceiling (13 bits) - lost-and-found is no longer exempt.
    // Note: precision must be <= MAX_POSITION_PRECISION_PUBLIC_KEY (15); well-known
    // channels always have a public PSK so getPositionPrecisionForChannel caps at 15.
    TEST_ASSERT_EQUAL_UINT32(13, out.precision_bits);
}

// ---------------------------------------------------------------------------
// Fuzz - crafted-nodenum blitz of the unified cache
// ---------------------------------------------------------------------------
// Floods handleReceived/alterReceived with crafted packets over a tiny node pool so the fixed-size
// cache churns hard while the virtual clock sweeps the rate/unknown/position windows; no crash, counters bounded.
static constexpr uint64_t FUZZ_SEED = 0x00D07E5701ULL;

static void test_tm_fuzz_nodenum_blitz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)FUZZ_SEED);
    rngSeed(FUZZ_SEED);

    // Activate the cache-tracked windows (the crafted-nodenum target). The nodeinfo direct-response
    // path (nodeinfo_direct_response_max_hops) is left OFF: it calls service->sendToMesh, and driving
    // that at fuzz volume needs a fully-wired MeshService/phone queue this fixture doesn't provide - the
    // deterministic test_tm_nodeinfo_directResponse_* tests cover it with single packets instead.
    moduleConfig.traffic_management.rate_limit_window_secs = 60;
    moduleConfig.traffic_management.rate_limit_max_packets = 3;
    moduleConfig.traffic_management.unknown_packet_threshold = 4;
    moduleConfig.traffic_management.position_min_interval_secs = 300;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    installWellKnownPrimaryChannel();

    static TrafficManagementModuleTestShim module; // static: OSThread-derived (see note in test_fuzz_packets E2)

    // Shared boundary pool (0/1/broadcast) plus this suite's well-known nodes.
    const NodeNum wellKnown[] = {kLocalNode, kRemoteNode, kTargetNode};
    const size_t wellKnownN = sizeof(wellKnown) / sizeof(wellKnown[0]);
    const meshtastic_PortNum ports[] = {
        meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_PortNum_NODEINFO_APP, meshtastic_PortNum_POSITION_APP,
        meshtastic_PortNum_ROUTING_APP,      meshtastic_PortNum_ADMIN_APP,    meshtastic_PortNum_TELEMETRY_APP,
    };
    const size_t portsN = sizeof(ports) / sizeof(ports[0]);

    const unsigned ITERS = 30000;
    for (unsigned k = 0; k < ITERS; k++) {
        meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
        p.from = (rngRange(8) == 0) ? (NodeNum)rngNext() : rngEdgeNodeNum(wellKnown, wellKnownN);
        p.to = rngEdgeNodeNum(wellKnown, wellKnownN);
        p.id = rngNext();
        p.channel = 0;
        p.hop_start = (uint8_t)rngRange(8); // 0..7, wire-bounded
        p.hop_limit = (uint8_t)rngRange(8);
        p.decoded.want_response = (rngRange(2) == 0);

        if (rngRange(5) == 0) {
            // Undecoded / unknown packet - exercises the unknown-packet threshold path.
            p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
            p.encrypted.size = rngRange(sizeof(p.encrypted.bytes) + 1);
            rngFill(p.encrypted.bytes, p.encrypted.size);
        } else {
            p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            p.decoded.portnum = (rngRange(8) == 0) ? (meshtastic_PortNum)rngRange(80) : ports[rngRange(portsN)];
            p.decoded.has_bitfield = true;
            p.decoded.bitfield = (uint32_t)rngNext();
            if (p.decoded.portnum == meshtastic_PortNum_POSITION_APP && rngRange(2)) {
                meshtastic_Position pos = meshtastic_Position_init_zero;
                pos.has_latitude_i = true;
                pos.has_longitude_i = true;
                pos.latitude_i = (int32_t)rngNext();
                pos.longitude_i = (int32_t)rngNext();
                pos.precision_bits = rngRange(40); // includes >32 (the default-precision fallback)
                p.decoded.payload.size =
                    pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_Position_msg, &pos);
            } else {
                // Random payload bytes: TMM's nested User/Position decode must fail cleanly.
                p.decoded.payload.size = rngRange(sizeof(p.decoded.payload.bytes) + 1);
                rngFill(p.decoded.payload.bytes, p.decoded.payload.size);
            }
        }

        (void)module.handleReceived(p);
        if (rngRange(3) == 0)
            module.alterReceived(p);

        // Advance the virtual clock so rate / unknown / position windows open and close under churn.
        if (rngRange(16) == 0)
            TrafficManagementModule::s_testNowMs += (rngRange(120) + 1) * 1000u;
        if (rngRange(1024) == 0)
            (void)module.runOnce(); // maintenance sweep: cache aging / eviction
    }

    // The cache never inspected more packets than we fed, and the run reached here without an ASan fault.
    TEST_ASSERT_TRUE_MESSAGE(module.getStats().packets_inspected <= ITERS, "packets_inspected overcounted");
}
} // namespace

void setUp(void)
{
    resetTrafficConfig();
}
void tearDown(void) {}

TM_TEST_ENTRY void setup()
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
    RUN_TEST(test_tm_fromUs_bypassesPositionAndRateFilters);
    RUN_TEST(test_tm_localDestination_bypassesTransitFilters);
    RUN_TEST(test_tm_nodeinfo_routerClamp_skipsWhenTooManyHops);
    RUN_TEST(test_tm_nodeinfo_directResponse_respondsFromCache);
    RUN_TEST(test_tm_nodeinfo_directResponse_learnsRequestorNodeInfo);
    RUN_TEST(test_tm_nodeinfo_clientClamp_skipsWhenNotDirect);
#if !(defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM))
    RUN_TEST(test_tm_nodeinfo_directResponse_withoutNodeDbEntry_skips);
#endif
#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
    RUN_TEST(test_tm_nodeinfo_directResponse_psramCacheRespondsAndPreservesBitfield);
    RUN_TEST(test_tm_nodeinfo_directResponse_psramMissDoesNotFallbackToNodeDb);
#endif
    RUN_TEST(test_tm_alterReceived_telemetryBroadcast_hopLimitUnchanged);
    RUN_TEST(test_tm_alterReceived_skipsLocalAndUnicast);
    RUN_TEST(test_tm_positionDedup_allowsDuplicateAfterIntervalExpires);
    RUN_TEST(test_tm_positionDedup_intervalZero_neverDrops);
    RUN_TEST(test_tm_positionDedup_precisionAbove32_usesDefaultPrecision);
    RUN_TEST(test_tm_positionDedup_distinctAtClampedChannelPrecision);
    RUN_TEST(test_tm_positionDedup_precisionZero_channelFallsBackToDefault);
    RUN_TEST(test_tm_positionDedup_cacheFlush_doesNotDropFirstPacketAfterFlush);
    RUN_TEST(test_tm_positionDedup_priorRateState_doesNotDropFirstFingerprintZero);
    RUN_TEST(test_tm_rateLimit_resetsAfterWindowExpires);
    RUN_TEST(test_tm_unknownPackets_resetAfterWindowExpires);
    RUN_TEST(test_tm_unknownPackets_thresholdAbove255_clamps);
    RUN_TEST(test_tm_alterReceived_positionBroadcast_hopLimitUnchanged);
    RUN_TEST(test_tm_alterReceived_skipsUndecodedPackets);
    RUN_TEST(test_tm_alterReceived_exhaustFlagAlwaysFalse);
    RUN_TEST(test_tm_alterReceived_exhaustFlag_isPacketScoped);
    RUN_TEST(test_tm_runOnce_disabledReturnsMaxInterval);
    RUN_TEST(test_tm_runOnce_enabledReturnsMaintenanceInterval);
    RUN_TEST(test_tm_nextHop_setAndGetRoundTrip);
    RUN_TEST(test_tm_nextHop_servedAfterNodeDbRoll);
    RUN_TEST(test_tm_nextHop_preloadDoesNotClobberLearned);
    RUN_TEST(test_tm_nextHop_keptAliveAcrossMaintenanceSweep);
    RUN_TEST(test_tm_trackerRole_capsDedupWindowAtOneHour);
    RUN_TEST(test_tm_takTrackerRole_capsDedupWindowAtOneHour);
    RUN_TEST(test_tm_trackerRole_survivesNodeDbEvictionViaCachedRole);
    RUN_TEST(test_tm_roleChange_viaNodeInfo_dropsTrackerException);
    RUN_TEST(test_tm_specialRole_pinsEntryThroughSweep);
    RUN_TEST(test_tm_specialRole_evictedLastUnderPressure);
    RUN_TEST(test_tm_trackerRole_doesNotLengthenShorterOperatorInterval);
    RUN_TEST(test_tm_lostAndFoundRole_capsDedupAtFifteenMinutes);
    RUN_TEST(test_tm_lostAndFoundRole_getsAlterReceivedPrecisionClamp);
    RUN_TEST(test_tm_unknownRole_noDbEntry_appliesFullInterval);
    RUN_TEST(test_tm_unknownRole_noUserBit_appliesFullInterval);
    RUN_TEST(test_tm_fuzz_nodenum_blitz);
    exit(UNITY_END());
}

TM_TEST_ENTRY void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}

TM_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

TM_TEST_ENTRY void loop() {}

#endif
