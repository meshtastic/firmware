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
#include "mesh/Router.h"
#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
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
#if ARCH_PORTDUINO
    bool forceSimRadio;
#endif
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
#if ARCH_PORTDUINO
    saved.forceSimRadio = portduino_config.force_simradio;
#endif

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
#if ARCH_PORTDUINO
    portduino_config.force_simradio = false;
#endif
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
#if ARCH_PORTDUINO
    portduino_config.force_simradio = saved.forceSimRadio;
#endif
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
    RUN_TEST(test_capture_endpoints_release_packet_pool_ownership);

    exit(UNITY_END());
}

EVENT_ROUTER_TEST_ENTRY void loop() {}
