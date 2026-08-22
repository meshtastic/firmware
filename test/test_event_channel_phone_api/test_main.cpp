#include "Channels.h"
#include "MeshModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "StreamAPI.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstring>
#include <memory>
#include <unity.h>
#include <vector>

namespace
{
constexpr PacketId BLOCKED_PACKET_ID = 0x10203040;
constexpr PacketId FOLLOWUP_PACKET_ID = 0x50607080;
constexpr PacketId WAYPOINT_PACKET_ID = 0x0a0b0c0d;
constexpr ChannelIndex EVENT_CHANNEL = 0;
constexpr ChannelIndex PRIVATE_CHANNEL = 1;
constexpr NodeNum LOCAL_NODE = 0x87654321;
constexpr NodeNum REMOTE_NODE = 0x12345678;

#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
// Where a coordinate packet the phone aimed at the event channel actually goes once a channel carries positions.
constexpr ChannelIndex COERCED_CHANNEL = PRIVATE_CHANNEL;
#else
constexpr ChannelIndex COERCED_CHANNEL = EVENT_CHANNEL;
#endif

// Router::sendLocal() loops a to-self packet through MeshModule::callModules(), which walks the module
// list; construct one so the list exists in this otherwise module-free binary.
class NoopModule : public MeshModule
{
  public:
    NoopModule() : MeshModule("event-phone-api-noop") {}
    bool wantPacket(const meshtastic_MeshPacket *) override { return false; }
};

class MockRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *packet) override
    {
        packetPool.release(packet);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
};

class MockRouter : public Router
{
  public:
    MockRouter() { addInterface(std::make_unique<MockRadioInterface>()); }

    ~MockRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    ErrorCode send(meshtastic_MeshPacket *packet) override
    {
        sentPackets.push_back(*packet);
        packetPool.release(packet);
        return ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
};

class MockMeshService : public MeshService
{
  public:
    ~MockMeshService()
    {
        while (auto *status = getQueueStatusForPhone()) {
            releaseQueueStatusToPool(status);
        }
    }

    void sendClientNotification(meshtastic_ClientNotification *notification) override
    {
        notifications.push_back(*notification);
        releaseClientNotificationToPool(notification);
    }

    void assertQueueStatus(PacketId packetId)
    {
        auto *status = getQueueStatusForPhone();
        TEST_ASSERT_NOT_NULL(status);
        TEST_ASSERT_EQUAL_UINT32(packetId, status->mesh_packet_id);
        releaseQueueStatusToPool(status);
    }

    std::vector<meshtastic_ClientNotification> notifications;
};

class TestStreamAPI : public StreamAPI
{
  public:
    TestStreamAPI() : StreamAPI(nullptr) {}
    bool checkIsConnected() override { return true; }
};

struct GlobalState {
    MeshService *service;
    Router *router;
    NodeDB *nodeDB;
    // Router's ctor asserts !cryptLock and allocates one; ~MockRouter() deletes it. Save the
    // incoming lock so the restored router keeps the one it was built with.
    concurrency::Lock *cryptLock;
    meshtastic_MyNodeInfo myNodeInfo;
    Channels channels;
    meshtastic_ChannelFile channelFile;
    meshtastic_LocalConfig config;
    meshtastic_LocalModuleConfig moduleConfig;
    meshtastic_DeviceState deviceState;
};

GlobalState *savedState;
MockMeshService *mockService;
MockRouter *mockRouter;
NodeDB *mockNodeDB;
TestStreamAPI *streamAPI;
NoopModule *noopModule;

void configureChannels()
{
    const meshtastic_ChannelFile defaultChannelFile = meshtastic_ChannelFile_init_default;
    channelFile = defaultChannelFile;
    channelFile.channels_count = 2;

    auto &eventChannel = channelFile.channels[EVENT_CHANNEL];
    eventChannel.index = EVENT_CHANNEL;
    eventChannel.has_settings = true;
    eventChannel.role = meshtastic_Channel_Role_PRIMARY;
    strncpy(eventChannel.settings.name, "everyone", sizeof(eventChannel.settings.name) - 1);
#ifdef USERPREFS_CHANNEL_0_PSK
    static const uint8_t eventPsk[] = USERPREFS_CHANNEL_0_PSK;
    eventChannel.settings.psk.size = sizeof(eventPsk);
    memcpy(eventChannel.settings.psk.bytes, eventPsk, sizeof(eventPsk));
#endif

    auto &privateChannel = channelFile.channels[PRIVATE_CHANNEL];
    privateChannel.index = PRIVATE_CHANNEL;
    privateChannel.has_settings = true;
    privateChannel.role = meshtastic_Channel_Role_SECONDARY;
    strncpy(privateChannel.settings.name, "private", sizeof(privateChannel.settings.name) - 1);
    privateChannel.settings.psk.size = 32;
    memset(privateChannel.settings.psk.bytes, 0xab, privateChannel.settings.psk.size);

    channels.onConfigChanged();
}

// configureChannels() leaves both channels without module_settings, i.e. position sharing off everywhere
// (getPositionPrecisionForChannel fails closed). Opt the private channel in so it becomes the position channel.
void enablePositionOnPrivateChannel()
{
    auto &privateChannel = channelFile.channels[PRIVATE_CHANNEL];
    privateChannel.settings.has_module_settings = true;
    privateChannel.settings.module_settings.position_precision = 32;
    channels.onConfigChanged();
}

meshtastic_ToRadio makeCoordinateToRadio(PacketId id, ChannelIndex channel, meshtastic_PortNum portnum, NodeNum to)
{
    meshtastic_ToRadio message = meshtastic_ToRadio_init_default;
    const meshtastic_MeshPacket defaultPacket = meshtastic_MeshPacket_init_default;
    message.which_payload_variant = meshtastic_ToRadio_packet_tag;
    message.packet = defaultPacket;
    message.packet.to = to;
    message.packet.id = id;
    message.packet.channel = channel;
    message.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    message.packet.decoded.portnum = portnum;
    return message;
}

meshtastic_ToRadio makePositionToRadio(PacketId id, ChannelIndex channel)
{
    return makeCoordinateToRadio(id, channel, meshtastic_PortNum_POSITION_APP, REMOTE_NODE);
}

bool sendToRadio(const meshtastic_ToRadio &message)
{
    uint8_t encoded[meshtastic_ToRadio_size] = {};
    const size_t encodedSize =
        pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ToRadio_msg, const_cast<meshtastic_ToRadio *>(&message));
    if (encodedSize == 0) {
        return false;
    }
    return streamAPI->handleToRadio(encoded, encodedSize);
}

void assertSentPacket(size_t index, PacketId id, ChannelIndex channel,
                      meshtastic_PortNum portnum = meshtastic_PortNum_POSITION_APP)
{
    TEST_ASSERT_GREATER_THAN(index, mockRouter->sentPackets.size());
    const auto &packet = mockRouter->sentPackets[index];
    TEST_ASSERT_EQUAL_UINT32(id, packet.id);
    TEST_ASSERT_EQUAL_UINT8(channel, packet.channel);
    TEST_ASSERT_EQUAL(portnum, packet.decoded.portnum);
}
} // namespace

void setUp(void)
{
    savedState =
        new GlobalState{service, router, nodeDB, cryptLock, myNodeInfo, channels, channelFile, config, moduleConfig, devicestate};

    service = mockService = new MockMeshService();
    nodeDB = mockNodeDB = new NodeDB();
    myNodeInfo.my_node_num = LOCAL_NODE;
    configureChannels();
    cryptLock = nullptr; // Router's ctor asserts this is unset before allocating its own.
    router = mockRouter = new MockRouter();
    streamAPI = new TestStreamAPI();
    noopModule = new NoopModule();
    testDelay(1);
}

void tearDown(void)
{
    delete noopModule;
    noopModule = nullptr;
    delete streamAPI;
    streamAPI = nullptr;
    delete mockRouter;
    mockRouter = nullptr;
    delete mockNodeDB;
    mockNodeDB = nullptr;
    delete mockService;
    mockService = nullptr;

    service = savedState->service;
    router = savedState->router;
    nodeDB = savedState->nodeDB;
    cryptLock = savedState->cryptLock; // ~MockRouter() nulled it; hand the saved router its own back.
    myNodeInfo = savedState->myNodeInfo;
    channels = savedState->channels;
    channelFile = savedState->channelFile;
    config = savedState->config;
    moduleConfig = savedState->moduleConfig;
    devicestate = savedState->deviceState;
    delete savedState;
    savedState = nullptr;
}

static void test_event_position_ingress_does_not_poison_retry_state()
{
    const auto eventAttempt = makePositionToRadio(BLOCKED_PACKET_ID, EVENT_CHANNEL);
    const auto sameIdPrivateRetry = makePositionToRadio(BLOCKED_PACKET_ID, PRIVATE_CHANNEL);
    const auto immediatePrivateFollowup = makePositionToRadio(FOLLOWUP_PACKET_ID, PRIVATE_CHANNEL);

#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    TEST_ASSERT_FALSE(sendToRadio(eventAttempt));
    TEST_ASSERT_EQUAL(0, mockRouter->sentPackets.size());
    mockService->assertQueueStatus(BLOCKED_PACKET_ID);
    TEST_ASSERT_EQUAL(1, mockService->notifications.size());
    TEST_ASSERT_EQUAL_UINT32(BLOCKED_PACKET_ID, mockService->notifications[0].reply_id);

    TEST_ASSERT_TRUE(sendToRadio(sameIdPrivateRetry));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    assertSentPacket(0, BLOCKED_PACKET_ID, PRIVATE_CHANNEL);
    mockService->assertQueueStatus(BLOCKED_PACKET_ID);

    TEST_ASSERT_FALSE(sendToRadio(immediatePrivateFollowup));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    mockService->assertQueueStatus(FOLLOWUP_PACKET_ID);
    TEST_ASSERT_EQUAL(1, mockService->notifications.size());
#else
    TEST_ASSERT_TRUE(sendToRadio(eventAttempt));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    assertSentPacket(0, BLOCKED_PACKET_ID, EVENT_CHANNEL);
    mockService->assertQueueStatus(BLOCKED_PACKET_ID);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());

    TEST_ASSERT_FALSE(sendToRadio(sameIdPrivateRetry));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    TEST_ASSERT_NULL(mockService->getQueueStatusForPhone());

    TEST_ASSERT_FALSE(sendToRadio(immediatePrivateFollowup));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    mockService->assertQueueStatus(FOLLOWUP_PACKET_ID);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());
#endif
}

// The apps feed the node its phone GPS fix as a POSITION packet addressed to the node itself on channel 0.
// That packet never leaves the device, so it must pass regardless of the event policy and without a
// notification, on any channel configuration (here: no channel carries positions at all).
static void test_phone_position_to_self_is_never_blocked()
{
    const auto toSelf = makeCoordinateToRadio(BLOCKED_PACKET_ID, EVENT_CHANNEL, meshtastic_PortNum_POSITION_APP, LOCAL_NODE);

    TEST_ASSERT_TRUE(sendToRadio(toSelf));
    TEST_ASSERT_EQUAL(0, mockRouter->sentPackets.size()); // delivered locally, never on the air
    mockService->assertQueueStatus(BLOCKED_PACKET_ID);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());
}

// A coordinate the phone aims at the event channel is moved onto the position channel (the first channel
// with position sharing enabled) instead of being rejected, and the phone is not told anything went wrong.
// Without the event policy the packet stays on the channel the phone chose.
static void test_phone_coordinates_on_event_channel_move_to_position_channel()
{
    enablePositionOnPrivateChannel();
    const auto positionRequest = makePositionToRadio(BLOCKED_PACKET_ID, EVENT_CHANNEL); // DM (e.g. "request position")
    const auto waypointBroadcast =
        makeCoordinateToRadio(WAYPOINT_PACKET_ID, EVENT_CHANNEL, meshtastic_PortNum_WAYPOINT_APP, NODENUM_BROADCAST);

    TEST_ASSERT_TRUE(sendToRadio(positionRequest));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    assertSentPacket(0, BLOCKED_PACKET_ID, COERCED_CHANNEL);
    mockService->assertQueueStatus(BLOCKED_PACKET_ID);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());

    TEST_ASSERT_TRUE(sendToRadio(waypointBroadcast));
    TEST_ASSERT_EQUAL(2, mockRouter->sentPackets.size());
    assertSentPacket(1, WAYPOINT_PACKET_ID, COERCED_CHANNEL, meshtastic_PortNum_WAYPOINT_APP);
    mockService->assertQueueStatus(WAYPOINT_PACKET_ID);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());
}

// A coordinate already on the position channel is left alone.
static void test_phone_coordinates_on_position_channel_are_untouched()
{
    enablePositionOnPrivateChannel();

    TEST_ASSERT_TRUE(sendToRadio(makePositionToRadio(FOLLOWUP_PACKET_ID, PRIVATE_CHANNEL)));
    TEST_ASSERT_EQUAL(1, mockRouter->sentPackets.size());
    assertSentPacket(0, FOLLOWUP_PACKET_ID, PRIVATE_CHANNEL);
    TEST_ASSERT_EQUAL(0, mockService->notifications.size());
}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_event_position_ingress_does_not_poison_retry_state);
    RUN_TEST(test_phone_position_to_self_is_never_blocked);
    RUN_TEST(test_phone_coordinates_on_event_channel_move_to_position_channel);
    RUN_TEST(test_phone_coordinates_on_position_channel_are_untouched);
    exit(UNITY_END());
}

void loop() {}
}
