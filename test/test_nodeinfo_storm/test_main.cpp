// NodeInfo storm suppression: the reply policy in src/modules/NodeInfoModule.cpp.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define STORM_TEST_ENTRY extern "C"
#else
#define STORM_TEST_ENTRY
#endif

#include "airtime.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/NodeInfoModule.h"
#include <cstdio>

namespace
{

constexpr NodeNum REQUESTER = 0x2222AAAA;

class StormRouter : public Router
{
  public:
    ~StormRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override { packetPool.release(p); }
};

class NodeInfoStormShim : public NodeInfoModule
{
  public:
    using MeshModule::currentRequest;
    using MeshModule::ignoreRequest;
    using NodeInfoModule::allocReply;
};

meshtastic_MeshPacket makeNodeInfoRequest(NodeNum to)
{
    // Every reply test hinges on the request looking like it came from someone else.
    TEST_ASSERT_NOT_EQUAL_MESSAGE(REQUESTER, nodeDB->getNodeNum(), "test requester must not be our own node number");

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = REQUESTER;
    p.to = to;
    p.id = 0x0000BEEF;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    p.decoded.want_response = true;
    return p;
}

// A want_response NodeInfo addressed to everyone is an amplification request: one packet, one
// reply per listener. Never answer it, whatever the database looks like.
void test_reply_refusedForBroadcastRequest(void)
{
    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(NODENUM_BROADCAST);
    shim.currentRequest = &req;

    TEST_ASSERT_NULL_MESSAGE(shim.allocReply(), "broadcast NodeInfo request must never be answered");
    TEST_ASSERT_TRUE_MESSAGE(shim.ignoreRequest, "a refused request must be ignored, not NAKed");
}

// The targeted half of the handshake still works: a node that unicasts a request to us - the
// unknown-node greeting, or a peer that could not decrypt our DM - gets an answer.
void test_reply_allowedForUnicastRequest(void)
{
    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(nodeDB->getNodeNum());
    shim.currentRequest = &req;

    meshtastic_MeshPacket *reply = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "a unicast NodeInfo request must still be answered");
    TEST_ASSERT_FALSE(shim.ignoreRequest);
    packetPool.release(reply);
}

// Our own scheduled broadcast is not a reply and must never be caught by the reply policy.
void test_periodicBroadcast_notSuppressed(void)
{
    NodeInfoStormShim shim;
    shim.currentRequest = nullptr; // not a reply - this is our own periodic send

    meshtastic_MeshPacket *p = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(p, "our scheduled NodeInfo broadcast must not be suppressed");
    packetPool.release(p);
}

} // namespace

void setUp(void) {}

void tearDown(void)
{
    NodeInfoStormShim::currentRequest = nullptr;
}

STORM_TEST_ENTRY void setup()
{
    initializeTestEnvironment();

    nodeDB = new NodeDB();
    router = new StormRouter();
    airTime = new AirTime();

    UNITY_BEGIN();

    printf("\n=== NodeInfo reply policy ===\n");
    RUN_TEST(test_reply_refusedForBroadcastRequest);
    RUN_TEST(test_reply_allowedForUnicastRequest);
    RUN_TEST(test_periodicBroadcast_notSuppressed);

    exit(UNITY_END());
}

STORM_TEST_ENTRY void loop() {}
