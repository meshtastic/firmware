#include "MeshModule.h"
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "modules/NeighborInfoModule.h"
#include "modules/NodeInfoModule.h"
#include "modules/RoutingModule.h"
#include "support/MockMeshService.h"
#include <memory>
#include <vector>

namespace
{
constexpr NodeNum LOCAL_NODE = 0x11111111;
constexpr NodeNum REMOTE_NODE = 0x22222222;

// Minimal concrete subclass for testing the base class helper
class TestModule : public MeshModule
{
  public:
    TestModule() : MeshModule("TestModule") {}
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    using MeshModule::currentRequest;
    using MeshModule::isMultiHopBroadcastRequest;
};

class MockNodeDB : public NodeDB
{
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

struct AckNak {
    meshtastic_Routing_Error error;
    NodeNum to;
    PacketId requestId;
    ChannelIndex channel;
};

class MockRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false) override
    {
        (void)hopLimit;
        (void)ackWantsAck;
        ackNaks.push_back({err, to, idFrom, chIndex});
    }

    std::vector<AckNak> ackNaks;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        (void)p;
        return false;
    }
};

class SyntheticReplyModule : public MeshModule
{
  public:
    SyntheticReplyModule(const char *name, meshtastic_PortNum modulePort, meshtastic_PortNum replyPort,
                         bool acceptsEveryPort = false)
        : MeshModule(name, modulePort), replyPort(replyPort), acceptsEveryPort(acceptsEveryPort)
    {
        isPromiscuous = acceptsEveryPort;
    }

    uint32_t allocReplyCalls = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return acceptsEveryPort || p->decoded.portnum == ourPortNum; }

    meshtastic_MeshPacket *allocReply() override
    {
        allocReplyCalls++;
        meshtastic_MeshPacket *reply = router->allocForSending();
        reply->decoded.portnum = replyPort;
        return reply;
    }

  private:
    meshtastic_PortNum replyPort;
    bool acceptsEveryPort;
};

class ZeroHopReplyModule : public SyntheticReplyModule
{
  public:
    ZeroHopReplyModule()
        : SyntheticReplyModule("zero-hop-reply", meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_TELEMETRY_APP)
    {
    }

  protected:
    uint8_t getResponseHopLimit(const meshtastic_MeshPacket &req) override
    {
        (void)req;
        return 0;
    }
};

class NodeInfoPolicyShim : public NodeInfoModule
{
  public:
    using NodeInfoModule::allocReply;
    using NodeInfoModule::getResponseHopLimit;
    using NodeInfoModule::handleReceivedProtobuf;
    using NodeInfoModule::isDirectBroadcastDiscoveryRequest;

    MeshModule *asMeshModule() { return this; }
    void setCurrentRequest(const meshtastic_MeshPacket *request) { currentRequest = request; }
};

class ObservingIgnoreModule : public MeshModule
{
  public:
    ObservingIgnoreModule() : MeshModule("storeforward-shaped", meshtastic_PortNum_STORE_FORWARD_APP) {}

    uint32_t allocReplyCalls = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        ignoreRequest = true;
        return ProcessMessage::CONTINUE;
    }

    meshtastic_MeshPacket *allocReply() override
    {
        allocReplyCalls++;
        return nullptr;
    }
};

class ReplyIgnoreModule : public MeshModule
{
  public:
    ReplyIgnoreModule() : MeshModule("reply-ignore", meshtastic_PortNum_NEIGHBORINFO_APP) {}

    uint32_t allocReplyCalls = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    meshtastic_MeshPacket *allocReply() override
    {
        allocReplyCalls++;
        ignoreRequest = true;
        return nullptr;
    }
};

static TestModule *testModule;
static meshtastic_MeshPacket testPacket;
static MockNodeDB *mockNodeDB;
static MockMeshService *mockService;
static MockRouter *mockRouter;
static MockRoutingModule *mockRoutingModule;
static NeighborInfoModule *realNeighborInfoModule;
static AirTime *testAirTime;
static std::vector<MeshModule *> dispatchModules;

template <typename T> static T *registerDispatchModule(T *module)
{
    dispatchModules.push_back(module);
    return module;
}

static meshtastic_MeshPacket makeRequest(meshtastic_PortNum port)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = REMOTE_NODE;
    packet.to = LOCAL_NODE;
    packet.id = 0x12345678;
    packet.channel = 0;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = port;
    packet.decoded.want_response = true;
    return packet;
}

static void dispatch(meshtastic_PortNum port)
{
    meshtastic_MeshPacket request = makeRequest(port);
    MeshModule::callModules(request);
}

static void dispatch(meshtastic_MeshPacket request)
{
    MeshModule::callModules(request);
}

} // namespace

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    channelFile = meshtastic_ChannelFile_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE;

    testAirTime = new AirTime();
    airTime = testAirTime;

    mockNodeDB = new MockNodeDB();
    nodeDB = mockNodeDB;
    myNodeInfo.my_node_num = LOCAL_NODE;

    mockService = new MockMeshService();
    service = mockService;

    channels.initDefaults();
    channels.onConfigChanged();

    mockRouter = new MockRouter();
    mockRouter->addInterface(std::unique_ptr<RadioInterface>(new MockRadioInterface()));
    router = mockRouter;

    mockRoutingModule = new MockRoutingModule();
    routingModule = mockRoutingModule;

    testModule = new TestModule();
    memset(&testPacket, 0, sizeof(testPacket));
    TestModule::currentRequest = &testPacket;
}

void tearDown(void)
{
    TestModule::currentRequest = NULL;

    for (auto it = dispatchModules.rbegin(); it != dispatchModules.rend(); ++it)
        delete *it;
    dispatchModules.clear();

    delete realNeighborInfoModule;
    realNeighborInfoModule = nullptr;

    delete testModule;
    testModule = nullptr;

    delete mockRoutingModule;
    mockRoutingModule = nullptr;
    routingModule = nullptr;

    while (auto *status = mockService->getQueueStatusForPhone())
        mockService->releaseQueueStatusToPool(status);
    while (auto *toPhone = mockService->getForPhone())
        mockService->releaseToPool(toPhone);
    delete mockService;
    mockService = nullptr;
    service = nullptr;

    delete mockRouter;
    mockRouter = nullptr;
    router = nullptr;

    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;

    airTime = nullptr;
    delete testAirTime;
    testAirTime = nullptr;
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

static void test_replyPortMatches_ownPort()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_TRUE(MeshModule::replyPortMatches(meshtastic_PortNum_TELEMETRY_APP, request));
}

static void test_replyPortMatches_neighborInfoVsTelemetry()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_FALSE(MeshModule::replyPortMatches(meshtastic_PortNum_NEIGHBORINFO_APP, request));
}

static void test_replyPortMatches_positionRequest()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_POSITION_APP);

    TEST_ASSERT_TRUE(MeshModule::replyPortMatches(meshtastic_PortNum_POSITION_APP, request));
}

static void test_replyPortMatches_unknownModulePort()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_FALSE(MeshModule::replyPortMatches(meshtastic_PortNum_UNKNOWN_APP, request));
}

static void test_replyPortMatches_unknownRequestPort()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_UNKNOWN_APP);

    TEST_ASSERT_FALSE(MeshModule::replyPortMatches(meshtastic_PortNum_TELEMETRY_APP, request));
}

static void test_dispatch_foreignPortOffenderCannotShadowOwner()
{
    auto *offender = registerDispatchModule(new SyntheticReplyModule("neighbor-shaped", meshtastic_PortNum_NEIGHBORINFO_APP,
                                                                     meshtastic_PortNum_NEIGHBORINFO_APP, true));
    auto *owner = registerDispatchModule(
        new SyntheticReplyModule("telemetry-owner", meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_TELEMETRY_APP));

    dispatch(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_EQUAL_UINT32(0, offender->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(1, owner->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TELEMETRY_APP, mockRouter->sentPackets[0].decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

static void test_dispatch_ownerPortStillReplies()
{
    auto *offender = registerDispatchModule(new SyntheticReplyModule("neighbor-shaped", meshtastic_PortNum_NEIGHBORINFO_APP,
                                                                     meshtastic_PortNum_NEIGHBORINFO_APP, true));
    auto *owner = registerDispatchModule(
        new SyntheticReplyModule("telemetry-owner", meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_TELEMETRY_APP));

    dispatch(meshtastic_PortNum_NEIGHBORINFO_APP);

    TEST_ASSERT_EQUAL_UINT32(1, offender->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(0, owner->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_NEIGHBORINFO_APP, mockRouter->sentPackets[0].decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

static void test_dispatch_crossPortReplyUsesRequestOwner()
{
    auto *position = registerDispatchModule(
        new SyntheticReplyModule("position-owner", meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_ATAK_PLUGIN_V2));

    dispatch(meshtastic_PortNum_POSITION_APP);

    TEST_ASSERT_EQUAL_UINT32(1, position->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_ATAK_PLUGIN_V2, mockRouter->sentPackets[0].decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

static void test_dispatch_moduleCanConstrainReplyHopLimit()
{
    registerDispatchModule(new ZeroHopReplyModule());

    dispatch(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT8(0, mockRouter->sentPackets[0].hop_limit);
}

static void test_nodeInfo_directBroadcastDiscoveryUsesZeroHopReply()
{
    NodeInfoPolicyShim nodeInfo;
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_NODEINFO_APP);
    request.to = NODENUM_BROADCAST;
    request.hop_start = 3;
    request.hop_limit = 3;
    request.decoded.has_bitfield = true;

    TEST_ASSERT_TRUE(NodeInfoPolicyShim::isDirectBroadcastDiscoveryRequest(request));
    TEST_ASSERT_EQUAL_UINT8(0, nodeInfo.getResponseHopLimit(request));
}

static void test_nodeInfo_relayedAndUnknownBroadcastDiscoveryDoNotQualify()
{
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_NODEINFO_APP);
    request.to = NODENUM_BROADCAST;
    request.hop_start = 3;
    request.hop_limit = 2;
    request.decoded.has_bitfield = true;

    TEST_ASSERT_FALSE(NodeInfoPolicyShim::isDirectBroadcastDiscoveryRequest(request));

    request.hop_start = 0;
    request.hop_limit = 0;
    request.decoded.has_bitfield = false;

    TEST_ASSERT_FALSE(NodeInfoPolicyShim::isDirectBroadcastDiscoveryRequest(request));
}

static void test_nodeInfo_unicastRequestRetainsRoutingHopLimit()
{
    NodeInfoPolicyShim nodeInfo;
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_NODEINFO_APP);

    TEST_ASSERT_FALSE(NodeInfoPolicyShim::isDirectBroadcastDiscoveryRequest(request));
    TEST_ASSERT_EQUAL_UINT8(mockRoutingModule->getHopLimitForResponse(request), nodeInfo.getResponseHopLimit(request));
}

static void test_nodeInfo_rejectedBroadcastDoesNotSuppressDirectDiscovery()
{
    auto *nodeInfo = new NodeInfoPolicyShim();
    dispatchModules.push_back(nodeInfo->asMeshModule());
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_NODEINFO_APP);
    request.to = NODENUM_BROADCAST;
    request.decoded.has_bitfield = true;
    request.hop_start = 3;
    request.hop_limit = 2;

    dispatch(request);
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());

    request.hop_start = 0;
    request.hop_limit = 0;
    request.decoded.has_bitfield = false;
    dispatch(request);
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());

    request.decoded.has_bitfield = true;
    dispatch(request);
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT8(0, mockRouter->sentPackets[0].hop_limit);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

static void test_nodeInfo_rejectedDirectRequestDoesNotSuppressDiscovery()
{
    NodeInfoPolicyShim nodeInfo;
    meshtastic_MeshPacket request = makeRequest(meshtastic_PortNum_NODEINFO_APP);
    request.to = NODENUM_BROADCAST;
    request.hop_start = 0;
    request.hop_limit = 0;
    request.decoded.has_bitfield = true;

    TEST_ASSERT_NOT_NULL(mockNodeDB->getOrCreateMeshNode(REMOTE_NODE));

    meshtastic_User rejectedUser = meshtastic_User_init_zero;
    rejectedUser.is_licensed = true;
    TEST_ASSERT_TRUE(nodeInfo.handleReceivedProtobuf(request, &rejectedUser));

    meshtastic_User validUser = meshtastic_User_init_zero;
    TEST_ASSERT_FALSE(nodeInfo.handleReceivedProtobuf(request, &validUser));

    nodeInfo.setCurrentRequest(&request);
    meshtastic_MeshPacket *reply = nodeInfo.allocReply();
    nodeInfo.setCurrentRequest(nullptr);

    TEST_ASSERT_NOT_NULL(reply);
    packetPool.release(reply);
}

static void test_dispatch_foreignPortObserverCanSuppressNak()
{
    auto *observer = registerDispatchModule(new ObservingIgnoreModule());

    dispatch(meshtastic_PortNum_TEXT_MESSAGE_APP);

    TEST_ASSERT_EQUAL_UINT32(0, observer->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

static void test_dispatch_noResponderSendsNak()
{
    dispatch(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NO_RESPONSE, mockRoutingModule->ackNaks[0].error);
    TEST_ASSERT_EQUAL_HEX32(REMOTE_NODE, mockRoutingModule->ackNaks[0].to);
}

static void test_dispatch_ignoreRequestIsClearedPerPacket()
{
    auto *ignoring = registerDispatchModule(new ReplyIgnoreModule());

    dispatch(meshtastic_PortNum_NEIGHBORINFO_APP);

    TEST_ASSERT_EQUAL_UINT32(1, ignoring->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());

    dispatch(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_EQUAL_UINT32(1, ignoring->allocReplyCalls);
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NO_RESPONSE, mockRoutingModule->ackNaks[0].error);
}

static void test_dispatch_realNeighborInfoCannotShadowTelemetryOwner()
{
    moduleConfig.neighbor_info.enabled = true;
    realNeighborInfoModule = new NeighborInfoModule();
    registerDispatchModule(
        new SyntheticReplyModule("telemetry-owner", meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_TELEMETRY_APP));

    dispatch(meshtastic_PortNum_TELEMETRY_APP);

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TELEMETRY_APP, mockRouter->sentPackets[0].decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

// A reply addressed to ourselves reaches the phone queue, with SHOULD_RELEASE reported as success.
static void test_localReplyToSelf_isDeliveredToPhone()
{
    meshtastic_MeshPacket reply = meshtastic_MeshPacket_init_zero;
    reply.from = LOCAL_NODE;
    reply.to = LOCAL_NODE;
    reply.id = 0xABCD1234;
    reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    reply.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    reply.decoded.request_id = 0x12345678;

    service->sendToMesh(packetPool.allocCopy(reply)); // default src == RX_SRC_LOCAL, as callModules sends replies

    meshtastic_MeshPacket *toPhone = mockService->getForPhone();
    TEST_ASSERT_NOT_NULL(toPhone);
    TEST_ASSERT_EQUAL_UINT32(0xABCD1234, toPhone->id);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, toPhone->decoded.request_id);
    mockService->releaseToPool(toPhone);
    TEST_ASSERT_NULL(mockService->getForPhone()); // exactly one delivery

    meshtastic_QueueStatus *qs = mockService->getQueueStatusForPhone();
    TEST_ASSERT_NOT_NULL(qs);
    TEST_ASSERT_EQUAL(ERRNO_OK, qs->res);
    mockService->releaseQueueStatusToPool(qs);

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size()); // nothing went toward the radio
}

// Full loop: a phone-originated want_response request (from == 0, RX_SRC_USER) dispatched
// through the real router must produce a module reply that reaches the phone queue.
static void test_phoneRequest_replyReachesPhone()
{
    auto *replyOwner = registerDispatchModule(
        new SyntheticReplyModule("private-owner", meshtastic_PortNum_PRIVATE_APP, meshtastic_PortNum_PRIVATE_APP));

    meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
    request.from = 0; // phone-originated, as MeshService::handleToRadio stamps it
    request.to = LOCAL_NODE;
    request.id = 0x5EED0001;
    request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    request.decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    request.decoded.want_response = true;

    service->sendToMesh(packetPool.allocCopy(request), RX_SRC_USER);

    TEST_ASSERT_EQUAL_UINT32(1, replyOwner->allocReplyCalls);

    meshtastic_MeshPacket *toPhone = mockService->getForPhone();
    TEST_ASSERT_NOT_NULL(toPhone);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_PRIVATE_APP, toPhone->decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(0x5EED0001, toPhone->decoded.request_id);
    TEST_ASSERT_EQUAL_UINT32(LOCAL_NODE, toPhone->to);
    mockService->releaseToPool(toPhone);
    TEST_ASSERT_NULL(mockService->getForPhone()); // the request itself must not echo back

    // One QueueStatus for the request, one for the reply, both reporting success
    uint32_t statuses = 0;
    while (auto *qs = mockService->getQueueStatusForPhone()) {
        TEST_ASSERT_EQUAL(ERRNO_OK, qs->res);
        mockService->releaseQueueStatusToPool(qs);
        statuses++;
    }
    TEST_ASSERT_EQUAL_UINT32(2, statuses);

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
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
    RUN_TEST(test_replyPortMatches_ownPort);
    RUN_TEST(test_replyPortMatches_neighborInfoVsTelemetry);
    RUN_TEST(test_replyPortMatches_positionRequest);
    RUN_TEST(test_replyPortMatches_unknownModulePort);
    RUN_TEST(test_replyPortMatches_unknownRequestPort);
    RUN_TEST(test_dispatch_foreignPortOffenderCannotShadowOwner);
    RUN_TEST(test_dispatch_ownerPortStillReplies);
    RUN_TEST(test_dispatch_crossPortReplyUsesRequestOwner);
    RUN_TEST(test_dispatch_moduleCanConstrainReplyHopLimit);
    RUN_TEST(test_nodeInfo_directBroadcastDiscoveryUsesZeroHopReply);
    RUN_TEST(test_nodeInfo_relayedAndUnknownBroadcastDiscoveryDoNotQualify);
    RUN_TEST(test_nodeInfo_unicastRequestRetainsRoutingHopLimit);
    RUN_TEST(test_nodeInfo_rejectedBroadcastDoesNotSuppressDirectDiscovery);
    RUN_TEST(test_nodeInfo_rejectedDirectRequestDoesNotSuppressDiscovery);
    RUN_TEST(test_dispatch_foreignPortObserverCanSuppressNak);
    RUN_TEST(test_dispatch_noResponderSendsNak);
    RUN_TEST(test_dispatch_ignoreRequestIsClearedPerPacket);
    RUN_TEST(test_dispatch_realNeighborInfoCannotShadowTelemetryOwner);
    RUN_TEST(test_localReplyToSelf_isDeliveredToPhone);
    RUN_TEST(test_phoneRequest_replyReachesPhone);
    exit(UNITY_END());
}

void loop() {}
