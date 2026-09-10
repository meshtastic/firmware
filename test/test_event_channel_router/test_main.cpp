#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshModule.h"
#include "mesh/MeshRadio.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/PositionPrecision.h"
#include "mesh/Router.h"
#include "modules/PositionModule.h"
#include "modules/RoutingModule.h"
#include "support/MockMeshService.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <pb_encode.h>
#include <vector>

#if ARCH_PORTDUINO
#define EVENT_ROUTER_TEST_ENTRY extern "C"
#else
#define EVENT_ROUTER_TEST_ENTRY
#endif

namespace
{

constexpr NodeNum kLocalNode = 0x11111111;
constexpr NodeNum kRemoteNode = 0x22222222;
constexpr NodeNum kPkiPeer = 0x33333333;
constexpr ChannelIndex kEventChannel = 0;
constexpr ChannelIndex kPrivateChannel = 1;

#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL
constexpr bool kBlockEventCoordinates = true;
constexpr ErrorCode kExpectedEventTxResult = meshtastic_Routing_Error_NOT_AUTHORIZED;
constexpr size_t kExpectedEventDeliveryCount = 0;
#else
constexpr bool kBlockEventCoordinates = false;
constexpr ErrorCode kExpectedEventTxResult = ERRNO_OK;
constexpr size_t kExpectedEventDeliveryCount = 3;
#endif

constexpr std::array<meshtastic_PortNum, 3> kCoordinatePorts = {
    meshtastic_PortNum_POSITION_APP,
    meshtastic_PortNum_WAYPOINT_APP,
    meshtastic_PortNum_MAP_REPORT_APP,
};

class TestNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    void addNode(NodeNum num, ChannelIndex channel, const uint8_t *publicKey = nullptr)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.channel = channel;
        if (publicKey) {
            node.public_key.size = 32;
            memcpy(node.public_key.bytes, publicKey, 32);
        }
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

  private:
    std::vector<meshtastic_NodeInfoLite> testNodes;
};

class CaptureRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *packet) override
    {
        packets.push_back(*packet);
        packetPool.release(packet);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t, bool = false) override { return 0; }

    std::vector<meshtastic_MeshPacket> packets;
};

class CaptureModule : public MeshModule
{
  public:
    CaptureModule() : MeshModule("event-router-capture") { encryptedOk = true; }

    bool wantPacket(const meshtastic_MeshPacket *) override { return true; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &packet) override
    {
        packets.push_back(packet);
        return ProcessMessage::CONTINUE;
    }

    std::vector<meshtastic_MeshPacket> packets;
};

struct SavedGlobals {
    meshtastic_LocalConfig config;
    meshtastic_LocalModuleConfig moduleConfig;
    meshtastic_ChannelFile channelFile;
    meshtastic_User owner;
    meshtastic_MyNodeInfo myNodeInfo;
    NodeDB *nodeDB;
    Router *router;
    MeshService *service;
    AirTime *airTime;
    concurrency::Lock *cryptLock;
};

SavedGlobals saved;
TestNodeDB *testNodeDB = nullptr;
Router *testRouter = nullptr;
CaptureRadio *captureRadio = nullptr;
CaptureModule *captureModule = nullptr;
AirTime *testAirTime = nullptr;

static void installChannels()
{
    memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = 2;

    meshtastic_Channel &event = channelFile.channels[kEventChannel];
    memset(&event, 0, sizeof(event));
    event.index = kEventChannel;
    event.role = meshtastic_Channel_Role_PRIMARY;
    event.has_settings = true;
    strncpy(event.settings.name, "everyone", sizeof(event.settings.name) - 1);
#ifdef USERPREFS_CHANNEL_0_PSK
    static const uint8_t eventKey[] = USERPREFS_CHANNEL_0_PSK;
    static_assert(sizeof(eventKey) == 16 || sizeof(eventKey) == 32);
    event.settings.psk.size = sizeof(eventKey);
    memcpy(event.settings.psk.bytes, eventKey, sizeof(eventKey));
#else
    static const uint8_t eventKey[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                         0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    event.settings.psk.size = sizeof(eventKey);
    memcpy(event.settings.psk.bytes, eventKey, sizeof(eventKey));
#endif

    meshtastic_Channel &privateChannel = channelFile.channels[kPrivateChannel];
    memset(&privateChannel, 0, sizeof(privateChannel));
    privateChannel.index = kPrivateChannel;
    privateChannel.role = meshtastic_Channel_Role_SECONDARY;
    privateChannel.has_settings = true;
    strncpy(privateChannel.settings.name, "private", sizeof(privateChannel.settings.name) - 1);
    privateChannel.settings.psk.size = 32;
    for (size_t i = 0; i < privateChannel.settings.psk.size; ++i)
        privateChannel.settings.psk.bytes[i] = static_cast<uint8_t>(0x80 + i);

    channels.onConfigChanged();
}

static meshtastic_MeshPacket makeDecodedPacket(meshtastic_PortNum port, NodeNum from, NodeNum to, ChannelIndex channel)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = from;
    packet.to = to;
    packet.id = 0x40000000u + static_cast<uint32_t>(port);
    packet.channel = channel;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = port;

    if (port == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position position = meshtastic_Position_init_zero;
        position.has_latitude_i = true;
        position.latitude_i = 374221234;
        position.has_longitude_i = true;
        position.longitude_i = -1220845678;
        packet.decoded.payload.size = pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                         &meshtastic_Position_msg, &position);
    } else {
        packet.decoded.payload.size = 1;
        packet.decoded.payload.bytes[0] = 0x5a;
    }
    return packet;
}

static ErrorCode sendCoordinate(meshtastic_PortNum port, ChannelIndex channel, NodeNum to = NODENUM_BROADCAST)
{
    meshtastic_MeshPacket *packet = testRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    const meshtastic_MeshPacket contents = makeDecodedPacket(port, kLocalNode, to, channel);
    packet->to = contents.to;
    packet->channel = contents.channel;
    packet->decoded = contents.decoded;
    return testRouter->send(packet);
}

static void receivePacket(const meshtastic_MeshPacket &contents)
{
    meshtastic_MeshPacket *packet = packetPool.allocCopy(contents);
    TEST_ASSERT_NOT_NULL(packet);
    testRouter->enqueueReceivedMessage(packet);
    testRouter->runOnce();
}

static void test_tx_event_channel_enforces_compile_time_policy_for_all_coordinate_ports()
{
    TEST_ASSERT_EQUAL(kBlockEventCoordinates, channels.isEventChannel(kEventChannel));

    for (meshtastic_PortNum port : kCoordinatePorts) {
        const size_t before = captureRadio->packets.size();
        TEST_ASSERT_EQUAL_INT(kExpectedEventTxResult, sendCoordinate(port, kEventChannel));
        TEST_ASSERT_EQUAL_UINT32(before + (kBlockEventCoordinates ? 0 : 1), captureRadio->packets.size());
    }
}

static void test_rx_event_channel_enforces_compile_time_policy_for_all_coordinate_ports()
{
    for (meshtastic_PortNum port : kCoordinatePorts)
        receivePacket(makeDecodedPacket(port, kRemoteNode, NODENUM_BROADCAST, kEventChannel));

    TEST_ASSERT_EQUAL_UINT32(kExpectedEventDeliveryCount, captureModule->packets.size());
}

static void test_private_channel_preserves_legacy_tx_and_rx_for_all_coordinate_ports()
{
    TEST_ASSERT_FALSE(channels.isEventChannel(kPrivateChannel));

    for (meshtastic_PortNum port : kCoordinatePorts) {
        TEST_ASSERT_EQUAL_INT(ERRNO_OK, sendCoordinate(port, kPrivateChannel));
        receivePacket(makeDecodedPacket(port, kRemoteNode, NODENUM_BROADCAST, kPrivateChannel));
    }

    TEST_ASSERT_EQUAL_UINT32(kCoordinatePorts.size(), captureRadio->packets.size());
    TEST_ASSERT_EQUAL_UINT32(kCoordinatePorts.size(), captureModule->packets.size());
}

#if !(MESHTASTIC_EXCLUDE_PKI)
static void test_tx_event_coordinate_that_uses_pki_reaches_radio()
{
    uint8_t peerPublic[32], peerPrivate[32];
    uint8_t localPublic[32], localPrivate[32];
    crypto->generateKeyPair(peerPublic, peerPrivate);
    crypto->generateKeyPair(localPublic, localPrivate);

    config.has_security = true;
    config.security.private_key.size = 32;
    config.security.public_key.size = 32;
    memcpy(config.security.private_key.bytes, localPrivate, 32);
    memcpy(config.security.public_key.bytes, localPublic, 32);
    crypto->setDHPrivateKey(localPrivate);
    testNodeDB->addNode(kPkiPeer, kEventChannel, peerPublic);

    TEST_ASSERT_EQUAL_INT(ERRNO_OK, sendCoordinate(meshtastic_PortNum_WAYPOINT_APP, kEventChannel, kPkiPeer));
    TEST_ASSERT_EQUAL_UINT32(1, captureRadio->packets.size());
    TEST_ASSERT_TRUE(captureRadio->packets.front().pki_encrypted);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, captureRadio->packets.front().which_payload_variant);
}
#endif

static void test_opaque_tx_is_not_misclassified_as_coordinates()
{
    meshtastic_MeshPacket *outgoing = testRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(outgoing);
    outgoing->channel = channels.getHash(kEventChannel);
    outgoing->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    outgoing->encrypted.size = 1;
    outgoing->encrypted.bytes[0] = 0xa5;

    TEST_ASSERT_EQUAL_INT(ERRNO_OK, testRouter->send(outgoing));
    TEST_ASSERT_EQUAL_UINT32(1, captureRadio->packets.size());
}

#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL
static void enablePositionOnPrivateChannel()
{
    meshtastic_Channel &privateChannel = channelFile.channels[kPrivateChannel];
    privateChannel.settings.has_module_settings = true;
    privateChannel.settings.module_settings.position_precision = 32;
    channels.onConfigChanged();
    uint8_t positionChannel = 0xff;
    TEST_ASSERT_TRUE(findPositionChannel(positionChannel));
    TEST_ASSERT_EQUAL_UINT8(kPrivateChannel, positionChannel);
}

// The reply path needs the module, a service to send through, a routing module for the response hop
// limit, and a fix of our own. Scoped to the one test so the rest of the suite stays module-free.
struct ReplyHarness {
    MeshService *savedService = service;
    RoutingModule *savedRouting = routingModule;
    PositionModule *savedPosition = positionModule;
    MockMeshService localService;
    RoutingModule localRouting;
    PositionModule localPosition;

    ReplyHarness()
    {
        service = &localService;
        routingModule = &localRouting;
        positionModule = &localPosition;
        testNodeDB->addNode(kLocalNode, kEventChannel); // refreshLocalMeshNode() asserts our own entry exists
        meshtastic_Position fix = meshtastic_Position_init_zero;
        fix.has_latitude_i = true;
        fix.latitude_i = 407825770;
        fix.has_longitude_i = true;
        fix.longitude_i = -1192084390;
        testNodeDB->setLocalPosition(fix);
    }

    ~ReplyHarness()
    {
        // Drain what sendToMesh() queued for the (absent) phone so the pools are clean at exit.
        while (auto *status = localService.getQueueStatusForPhone())
            localService.releaseQueueStatusToPool(status);
        while (auto *packet = localService.getForPhone())
            localService.releaseToPool(packet);
        positionModule = savedPosition;
        routingModule = savedRouting;
        service = savedService;
    }
};

// A position request DM'd to us on the event channel is not processed (no module sees it, so nothing is
// stored or forwarded), but it is answered: our position goes out as a reply, on the position channel.
static void test_rx_event_channel_position_request_to_us_is_answered_on_position_channel()
{
    enablePositionOnPrivateChannel();
    ReplyHarness harness;

    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kRemoteNode, kLocalNode, kEventChannel);
    request.decoded.want_response = true;
    receivePacket(request);

    TEST_ASSERT_EQUAL_UINT32(0, captureModule->packets.size());
    TEST_ASSERT_EQUAL_UINT32(1, captureRadio->packets.size());

    meshtastic_MeshPacket reply = captureRadio->packets.front();
    TEST_ASSERT_EQUAL_UINT32(kRemoteNode, reply.to);
    TEST_ASSERT_EQUAL_UINT32(kLocalNode, reply.from);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, reply.which_payload_variant); // went out under a channel key
    TEST_ASSERT_EQUAL(DecodeState::DECODE_SUCCESS, perhapsDecode(&reply));
    TEST_ASSERT_EQUAL_UINT8(kPrivateChannel, reply.channel); // ...the position channel's, not the event channel's
    TEST_ASSERT_EQUAL(meshtastic_PortNum_POSITION_APP, reply.decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(request.id, reply.decoded.request_id);
}

// Without a position channel there is nothing to answer on: the request is simply dropped.
static void test_rx_event_channel_position_request_without_position_channel_is_dropped()
{
    ReplyHarness harness;

    meshtastic_MeshPacket request = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kRemoteNode, kLocalNode, kEventChannel);
    request.decoded.want_response = true;
    receivePacket(request);

    TEST_ASSERT_EQUAL_UINT32(0, captureModule->packets.size());
    TEST_ASSERT_EQUAL_UINT32(0, captureRadio->packets.size());
}

// A broadcast position on the event channel is dropped outright, want_response or not: only unicast
// requests to us are answered.
static void test_rx_event_channel_position_broadcast_with_want_response_is_not_answered()
{
    enablePositionOnPrivateChannel();
    ReplyHarness harness;

    meshtastic_MeshPacket broadcast =
        makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kRemoteNode, NODENUM_BROADCAST, kEventChannel);
    broadcast.decoded.want_response = true;
    receivePacket(broadcast);

    TEST_ASSERT_EQUAL_UINT32(0, captureModule->packets.size());
    TEST_ASSERT_EQUAL_UINT32(0, captureRadio->packets.size());
}

// The phone hands a GPS-less node its fix as a POSITION packet from us to us on channel 0. It never goes on
// the air, so the event policy must let it through to the modules (where PositionModule records it).
static void test_loopback_position_from_us_to_us_on_event_channel_is_not_blocked()
{
    meshtastic_MeshPacket loopback = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kLocalNode, kLocalNode, kEventChannel);
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&loopback));

    meshtastic_MeshPacket *packet = packetPool.allocCopy(loopback);
    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_EQUAL_INT(ERRNO_SHOULD_RELEASE, testRouter->sendLocal(packet, RX_SRC_USER));
    packetPool.release(packet);

    TEST_ASSERT_EQUAL_UINT32(1, captureModule->packets.size());
    TEST_ASSERT_EQUAL_UINT32(0, captureRadio->packets.size());
}

// A local originator (module, UI) that aims a coordinate at the event channel is moved onto the position
// channel by sendLocal(); with no position channel the send is still refused.
static void test_tx_local_coordinate_on_event_channel_is_moved_to_position_channel()
{
    meshtastic_MeshPacket *packet = testRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    packet->to = NODENUM_BROADCAST;
    packet->channel = kEventChannel;
    packet->decoded = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kLocalNode, NODENUM_BROADCAST, kEventChannel).decoded;
    TEST_ASSERT_EQUAL_INT(meshtastic_Routing_Error_NOT_AUTHORIZED, testRouter->sendLocal(packet, RX_SRC_LOCAL));
    TEST_ASSERT_EQUAL_UINT32(0, captureRadio->packets.size());

    enablePositionOnPrivateChannel();
    packet = testRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(packet);
    packet->to = NODENUM_BROADCAST;
    packet->channel = kEventChannel;
    packet->decoded = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, kLocalNode, NODENUM_BROADCAST, kEventChannel).decoded;
    TEST_ASSERT_EQUAL_INT(ERRNO_OK, testRouter->sendLocal(packet, RX_SRC_LOCAL));
    TEST_ASSERT_EQUAL_UINT32(1, captureRadio->packets.size());

    meshtastic_MeshPacket sent = captureRadio->packets.front();
    TEST_ASSERT_EQUAL(DecodeState::DECODE_SUCCESS, perhapsDecode(&sent));
    TEST_ASSERT_EQUAL_UINT8(kPrivateChannel, sent.channel);
}
#endif

static void test_capture_endpoints_release_packet_pool_ownership()
{
    constexpr size_t iterations = 64;
    for (size_t i = 0; i < iterations; ++i) {
        meshtastic_MeshPacket *outgoing = testRouter->allocForSending();
        TEST_ASSERT_NOT_NULL(outgoing);
        outgoing->channel = kPrivateChannel;
        outgoing->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        outgoing->decoded.payload.size = 1;
        outgoing->decoded.payload.bytes[0] = static_cast<uint8_t>(i);
        TEST_ASSERT_EQUAL_INT(ERRNO_OK, testRouter->send(outgoing));

        meshtastic_MeshPacket incoming =
            makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, NODENUM_BROADCAST, kPrivateChannel);
        incoming.id += i;
        receivePacket(incoming);
    }

    TEST_ASSERT_EQUAL_UINT32(iterations, captureRadio->packets.size());
    TEST_ASSERT_EQUAL_UINT32(iterations, captureModule->packets.size());
}

} // namespace

void setUp(void)
{
    saved.config = config;
    saved.moduleConfig = moduleConfig;
    saved.channelFile = channelFile;
    saved.owner = owner;
    saved.myNodeInfo = myNodeInfo;
    saved.nodeDB = nodeDB;
    saved.router = router;
    saved.service = service;
    saved.airTime = airTime;
    saved.cryptLock = cryptLock;

    testNodeDB = new TestNodeDB();
    testNodeDB->clearTestNodes();
    nodeDB = testNodeDB;

    memset(&config, 0, sizeof(config));
    config.lora.override_duty_cycle = true;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    memset(&moduleConfig, 0, sizeof(moduleConfig));
    memset(&owner, 0, sizeof(owner));
    memset(&myNodeInfo, 0, sizeof(myNodeInfo));
    myNodeInfo.my_node_num = kLocalNode;
    service = nullptr;
    installChannels();

    testAirTime = new AirTime();
    airTime = testAirTime;

    cryptLock = nullptr;
    testRouter = new Router();
    router = testRouter;
    std::unique_ptr<CaptureRadio> radio(new CaptureRadio());
    captureRadio = radio.get();
    testRouter->addInterface(std::move(radio));
    captureModule = new CaptureModule();
}

void tearDown(void)
{
    delete captureModule;
    captureModule = nullptr;

    router = nullptr;
    delete testRouter;
    testRouter = nullptr;
    captureRadio = nullptr;
    delete cryptLock;
    cryptLock = saved.cryptLock;

    delete testNodeDB;
    testNodeDB = nullptr;
    delete testAirTime;
    testAirTime = nullptr;

    config = saved.config;
    moduleConfig = saved.moduleConfig;
    channelFile = saved.channelFile;
    owner = saved.owner;
    myNodeInfo = saved.myNodeInfo;
    channels.onConfigChanged();
    nodeDB = saved.nodeDB;
    router = saved.router;
    service = saved.service;
    airTime = saved.airTime;
}

EVENT_ROUTER_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Router event-channel coordinate enforcement ===\n");
    RUN_TEST(test_tx_event_channel_enforces_compile_time_policy_for_all_coordinate_ports);
    RUN_TEST(test_rx_event_channel_enforces_compile_time_policy_for_all_coordinate_ports);
    RUN_TEST(test_private_channel_preserves_legacy_tx_and_rx_for_all_coordinate_ports);
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_tx_event_coordinate_that_uses_pki_reaches_radio);
#endif
    RUN_TEST(test_opaque_tx_is_not_misclassified_as_coordinates);
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL
    RUN_TEST(test_rx_event_channel_position_request_to_us_is_answered_on_position_channel);
    RUN_TEST(test_rx_event_channel_position_request_without_position_channel_is_dropped);
    RUN_TEST(test_rx_event_channel_position_broadcast_with_want_response_is_not_answered);
    RUN_TEST(test_loopback_position_from_us_to_us_on_event_channel_is_not_blocked);
    RUN_TEST(test_tx_local_coordinate_on_event_channel_is_moved_to_position_channel);
#endif
    RUN_TEST(test_capture_endpoints_release_packet_pool_ownership);

    exit(UNITY_END());
}

EVENT_ROUTER_TEST_ENTRY void loop() {}
