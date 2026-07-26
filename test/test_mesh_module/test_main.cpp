#include "MeshModule.h"
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "configuration.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "modules/NeighborInfoModule.h"
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

// Sends, from inside callModules(), a self-addressed reply and a local broadcast - the fan-out an
// AdminModule config save performs. Both go out through service->sendToMesh() while a
// handleReceived() frame is live, so both must be deferred rather than handled re-entrantly.
class NestedFanoutModule : public MeshModule
{
  public:
    NestedFanoutModule() : MeshModule("nested-fanout", meshtastic_PortNum_PRIVATE_APP) {}
    uint32_t handleCalls = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        handleCalls++;

        // (a) a reply addressed to ourselves - exercises the isToUs() deferral branch
        meshtastic_MeshPacket *reply = router->allocForSending();
        reply->to = LOCAL_NODE;
        reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        reply->decoded.request_id = 0xBEEF;
        service->sendToMesh(reply); // default src == RX_SRC_LOCAL

        // (b) a local broadcast - exercises the broadcast branch (loopback deferred, TX immediate)
        meshtastic_MeshPacket *bcast = router->allocForSending();
        bcast->to = NODENUM_BROADCAST;
        bcast->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        service->sendToMesh(bcast);

        return ProcessMessage::STOP;
    }
};

// loopbackOk so it also runs for drained RX_SRC_LOCAL packets; each generation sends the next, so
// the drain must keep processing a chain that grows while it is draining.
class ChainSendModule : public MeshModule
{
  public:
    ChainSendModule() : MeshModule("chain-send", meshtastic_PortNum_PRIVATE_APP) { loopbackOk = true; }
    uint32_t handleCalls = 0;
    uint32_t maxGeneration = 0;

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        handleCalls++;
        uint32_t generation = mp.decoded.request_id;
        if (generation > maxGeneration)
            maxGeneration = generation;

        if (generation < 3) {
            meshtastic_MeshPacket *next = router->allocForSending();
            next->to = LOCAL_NODE;
            next->decoded.portnum = ourPortNum;
            next->decoded.request_id = generation + 1;
            service->sendToMesh(next); // default src == RX_SRC_LOCAL
        }
        return ProcessMessage::STOP;
    }
};

// Sends a burst of self-addressed replies from one dispatch, to overflow the deferred queue.
class BurstSendModule : public MeshModule
{
  public:
    explicit BurstSendModule(uint32_t count) : MeshModule("burst-send", meshtastic_PortNum_PRIVATE_APP), count(count) {}

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        for (uint32_t i = 0; i < count; i++) {
            meshtastic_MeshPacket *reply = router->allocForSending();
            reply->to = LOCAL_NODE;
            reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            reply->decoded.request_id = 0x100 + i;
            service->sendToMesh(reply); // default src == RX_SRC_LOCAL
        }
        return ProcessMessage::STOP;
    }

  private:
    uint32_t count;
};

static TestModule *testModule;
static meshtastic_MeshPacket testPacket;
static MockNodeDB *mockNodeDB;
static MockMeshService *mockService;
static MockRouter *mockRouter;
static MockRoutingModule *mockRoutingModule;
static NeighborInfoModule *realNeighborInfoModule;
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

// Dispatch a want_response==false trigger addressed to us through the real router, so a module's
// handler runs inside a live handleReceived() frame (handleDepth == 1) and any packet it sends is
// deferred. requestId is carried in decoded.request_id for modules that key on a generation.
static void dispatchTrigger(meshtastic_PortNum port, uint32_t requestId = 0)
{
    meshtastic_MeshPacket trigger = meshtastic_MeshPacket_init_zero;
    trigger.from = LOCAL_NODE;
    trigger.to = LOCAL_NODE;
    trigger.id = 0x7A190000 + requestId;
    trigger.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    trigger.decoded.portnum = port;
    trigger.decoded.request_id = requestId;
    service->sendToMesh(packetPool.allocCopy(trigger), RX_SRC_USER);
}

} // namespace

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    channelFile = meshtastic_ChannelFile_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE;

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

// A module that fans out a self-addressed reply and a local broadcast from inside callModules()
// must not re-enter handleReceived(): the sends are deferred and drained flat (max depth 1).
static void test_nestedLocalSend_isDeferred_notReentrant()
{
    auto *mod = registerDispatchModule(new NestedFanoutModule());

    dispatchTrigger(meshtastic_PortNum_PRIVATE_APP);

    // The module ran once and was never re-entered; the drain left nothing behind.
    TEST_ASSERT_EQUAL_UINT32(1, mod->handleCalls);
    TEST_ASSERT_EQUAL_UINT8(1, mockRouter->maxHandleDepthObserved);
    TEST_ASSERT_EQUAL_UINT8(0, mockRouter->deferredLocalPending());

    // The self-addressed reply reached the phone exactly once (composition with #11185's ccToPhone).
    meshtastic_MeshPacket *toPhone = mockService->getForPhone();
    TEST_ASSERT_NOT_NULL(toPhone);
    TEST_ASSERT_EQUAL_UINT32(LOCAL_NODE, toPhone->to);
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, toPhone->decoded.request_id);
    mockService->releaseToPool(toPhone);
    TEST_ASSERT_NULL(mockService->getForPhone()); // the broadcast did not also go to the phone

    // The broadcast still reached the radio TX path exactly once.
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_TRUE(isBroadcast(mockRouter->sentPackets[0].to));
}

// A drained packet whose own module sends again is picked up by the same drain pass, so a chain of
// local sends is processed breadth-first with the stack held flat (max depth 1).
static void test_deferredChain_drainsBreadthFirst()
{
    auto *mod = registerDispatchModule(new ChainSendModule());

    dispatchTrigger(meshtastic_PortNum_PRIVATE_APP, 1); // generation 1

    // Generations 1 -> 2 -> 3 were all processed in the single outermost drain, never nesting.
    TEST_ASSERT_EQUAL_UINT32(3, mod->handleCalls);
    TEST_ASSERT_EQUAL_UINT32(3, mod->maxGeneration);
    TEST_ASSERT_EQUAL_UINT8(1, mockRouter->maxHandleDepthObserved);
    TEST_ASSERT_EQUAL_UINT8(0, mockRouter->deferredLocalPending());
}

// Overflowing the fixed deferred queue drops the excess deferrals gracefully: no crash, no leak,
// depth still capped, and every send's phone cc still happens (return codes unchanged).
static void test_deferredQueueOverflow_dropsGracefully()
{
    const uint32_t burst = 6; // deferredLocalCapacity (4) + 2 - keep in sync with Router.h

    registerDispatchModule(new BurstSendModule(burst));

    dispatchTrigger(meshtastic_PortNum_PRIVATE_APP);

    // Two deferrals were dropped (queue full) and nothing re-entered handleReceived().
    TEST_ASSERT_EQUAL_UINT32(2, mockRouter->deferredLocalDropped);
    TEST_ASSERT_EQUAL_UINT8(1, mockRouter->maxHandleDepthObserved);
    TEST_ASSERT_EQUAL_UINT8(0, mockRouter->deferredLocalPending()); // drained clean

    // Return codes were unchanged: every reply still cc'd to the phone, dropped deferral or not.
    uint32_t delivered = 0;
    while (auto *toPhone = mockService->getForPhone()) {
        mockService->releaseToPool(toPhone);
        delivered++;
    }
    TEST_ASSERT_EQUAL_UINT32(burst, delivered);
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
    RUN_TEST(test_dispatch_foreignPortObserverCanSuppressNak);
    RUN_TEST(test_dispatch_noResponderSendsNak);
    RUN_TEST(test_dispatch_ignoreRequestIsClearedPerPacket);
    RUN_TEST(test_dispatch_realNeighborInfoCannotShadowTelemetryOwner);
    RUN_TEST(test_localReplyToSelf_isDeliveredToPhone);
    RUN_TEST(test_phoneRequest_replyReachesPhone);
    RUN_TEST(test_nestedLocalSend_isDeferred_notReentrant);
    RUN_TEST(test_deferredChain_drainsBreadthFirst);
    RUN_TEST(test_deferredQueueOverflow_dropsGracefully);
    exit(UNITY_END());
}

void loop() {}
