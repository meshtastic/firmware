// NodeInfo storm suppression: the NodeDB churn detector (src/mesh/NodeDB.cpp) and the reply
// policy it feeds in src/modules/NodeInfoModule.cpp.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define STORM_TEST_ENTRY extern "C"
#else
#define STORM_TEST_ENTRY
#endif

#include "airtime.h"
#include "gps/RTC.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/NodeInfoModule.h"
#include <cstdio>
#include <cstring>

// Global scope so it matches `friend class NodeDBTestShim` in NodeDB.h - the churn ring and
// noteNodeEvicted() are private.
class NodeDBTestShim : public NodeDB
{
  public:
    void resetChurn()
    {
        memset(freshEvictions, 0, sizeof(freshEvictions));
        freshEvictionHead = 0;
        freshEvictionsWrapped = false;
    }

    // Fill the hot store to capacity with nodes last heard ageSecs ago; index 0 is us.
    void fill(uint32_t ageSecs)
    {
        meshNodes->clear();
        numMeshNodes = 0;
        push(0x0BADF00D, getTime());
        for (int i = 1; i < MAX_NUM_NODES; i++)
            push(0x00010000 + i, getTime() - ageSecs);
        TEST_ASSERT_TRUE(isFull());
    }

    // Admit a node the way updateFrom() does: create it (evicting when full), then stamp when we
    // heard it. Without the stamp the fresh arrival is itself the next eviction victim.
    void admit(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getOrCreateMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        n->last_heard = getTime();
        nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
    }

  private:
    void push(NodeNum num, uint32_t lastHeard)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = lastHeard;
        nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();
    }
};

namespace
{

constexpr NodeNum REQUESTER = 0x2222AAAA;
constexpr NodeNum FRESH_BASE = 0x77000000;

NodeDBTestShim *db = nullptr;

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
    TEST_ASSERT_NOT_EQUAL_MESSAGE(REQUESTER, db->getNodeNum(), "test requester must not be our own node number");

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = REQUESTER;
    p.to = to;
    p.id = 0x0000BEEF;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    p.decoded.want_response = true;
    return p;
}

// Drive `count` real evictions through getOrCreateMeshNode, using a fresh block of node numbers
// each time so nothing is re-admitted out of the warm tier.
void churn(int count, uint32_t &seq)
{
    for (int i = 0; i < count; i++)
        db->admit(FRESH_BASE + (seq++));
}

// ---------------------------------------------------------------------------
// NodeDB churn detection
// ---------------------------------------------------------------------------

// Evicting nodes we have not heard from in hours is ordinary pruning, not churn.
void test_rolling_falseWhenEvictingStaleNodes(void)
{
    uint32_t seq = 1000;
    db->fill(NODEDB_ROLL_FRESH_SECS + 600); // every victim is well past the freshness cutoff

    churn(NODEDB_ROLL_SAMPLES * 2, seq);

    TEST_ASSERT_FALSE_MESSAGE(db->isNodeDbRolling(), "pruning stale entries must not read as churn");
}

// Evicting nodes we can still hear, repeatedly, means the mesh outgrew the database.
void test_rolling_trueAfterFreshEvictions(void)
{
    uint32_t seq = 2000;
    db->fill(60); // every victim was heard a minute ago

    churn(NODEDB_ROLL_SAMPLES, seq);

    TEST_ASSERT_TRUE_MESSAGE(db->isNodeDbRolling(), "evicting nodes we still hear must read as churn");
}

// One sample short of the ring is not enough evidence - a couple of unlucky evictions on an
// otherwise healthy mesh must not mute our NodeInfo replies.
void test_rolling_needsAFullRingOfSamples(void)
{
    uint32_t seq = 3000;
    db->fill(60);

    churn(NODEDB_ROLL_SAMPLES - 1, seq);
    TEST_ASSERT_FALSE_MESSAGE(db->isNodeDbRolling(), "a partial ring must not trip the detector");

    churn(1, seq);
    TEST_ASSERT_TRUE_MESSAGE(db->isNodeDbRolling(), "the sample that fills the ring must trip it");
}

// ---------------------------------------------------------------------------
// NodeInfoModule reply policy
// ---------------------------------------------------------------------------

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
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    shim.currentRequest = &req;

    meshtastic_MeshPacket *reply = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "a unicast NodeInfo request must still be answered");
    TEST_ASSERT_FALSE(shim.ignoreRequest);
    packetPool.release(reply);
}

// On a rolling database even the unicast reply is deferred to our scheduled broadcast.
void test_reply_refusedForUnicastWhileRolling(void)
{
    uint32_t seq = 4000;
    db->fill(60);
    churn(NODEDB_ROLL_SAMPLES, seq);
    TEST_ASSERT_TRUE(db->isNodeDbRolling());

    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    shim.currentRequest = &req;

    TEST_ASSERT_NULL_MESSAGE(shim.allocReply(), "a rolling database must defer the reply");
    TEST_ASSERT_TRUE(shim.ignoreRequest);
}

// The scheduled broadcast is the thing we defer *to*, so it must survive both refusals.
void test_periodicBroadcast_survivesRolling(void)
{
    uint32_t seq = 5000;
    db->fill(60);
    churn(NODEDB_ROLL_SAMPLES, seq);
    TEST_ASSERT_TRUE(db->isNodeDbRolling());

    NodeInfoStormShim shim;
    shim.currentRequest = nullptr; // not a reply - this is our own periodic send

    meshtastic_MeshPacket *p = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(p, "our scheduled NodeInfo broadcast must not be suppressed");
    packetPool.release(p);
}

} // namespace

void setUp(void)
{
    db->resetChurn();
}

void tearDown(void)
{
    NodeInfoStormShim::currentRequest = nullptr;
}

STORM_TEST_ENTRY void setup()
{
    initializeTestEnvironment();

    db = new NodeDBTestShim();
    nodeDB = db;
    router = new StormRouter();
    airTime = new AirTime();

    UNITY_BEGIN();

    printf("\n=== NodeDB churn detection ===\n");
    RUN_TEST(test_rolling_falseWhenEvictingStaleNodes);
    RUN_TEST(test_rolling_trueAfterFreshEvictions);
    RUN_TEST(test_rolling_needsAFullRingOfSamples);

    printf("\n=== NodeInfo reply policy ===\n");
    RUN_TEST(test_reply_refusedForBroadcastRequest);
    RUN_TEST(test_reply_allowedForUnicastRequest);
    RUN_TEST(test_reply_refusedForUnicastWhileRolling);
    RUN_TEST(test_periodicBroadcast_survivesRolling);

    exit(UNITY_END());
}

STORM_TEST_ENTRY void loop() {}
