// NodeInfo storm suppression: the NodeDB probation band (src/mesh/NodeDB.cpp), the greeting gate
// in src/mesh/MeshService.cpp and the reply policy in src/modules/NodeInfoModule.cpp that read it.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define PROBATION_TEST_ENTRY extern "C"
#else
#define PROBATION_TEST_ENTRY
#endif

#include "UptimeClock.h"
#include "airtime.h"
#include "gps/RTC.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/NodeInfoModule.h"
#include <cstdio>
#include <cstring>

// Global scope so it matches `friend class NodeDBTestShim` in NodeDB.h - the probation helpers
// and the residency EMA are private.
class NodeDBTestShim : public NodeDB
{
  public:
    void resetProbation() { probationResidencyEmaSecs = 2 * NODEDB_PROBATION_GAP_MAX_SECS; }
    int residentsEvicted = 0; // counted by the shim's promote(), see below

    using NodeDB::probationCount;
    int residents() const { return numMeshNodes - probationCount(); }
    bool onProbation(NodeNum num) { return nodeInfoLiteIsOnProbation(getMeshNode(num)); }
    void promote(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        const int before = numMeshNodes;
        promoteFromProbation(n);
        residentsEvicted += before - numMeshNodes;
    }

    // Deliver a decoded packet the way the radio path does, so updateFrom() runs its probation
    // logic. rxTime is an epoch; toUs addresses the packet to our own node number.
    void hear(NodeNum from, uint32_t rxTime, bool toUs = false)
    {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = from;
        mp.to = toUs ? getNodeNum() : NODENUM_BROADCAST;
        mp.id = 0x1000 + from;
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        mp.has_rx_time = true;
        mp.rx_time = rxTime;
        mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
        // Step past the full-store admission throttle (NODEDB_FULL_EVICTION_INTERVAL_MS, 2 s); the
        // unsigned subtraction is wrap-safe when the test clock is young.
        lastFullEvictionMs = Time::getMillis() - 3000;
        updateFrom(mp);
    }

    // Fill the hot store to capacity with nodes carrying the given last_heard; index 0 is us.
    // 0 is what every entry holds on a boot whose clock is not yet trusted.
    void fillWithLastHeard(uint32_t lastHeard)
    {
        meshNodes->clear();
        numMeshNodes = 0;
        push(0x0BADF00D, getTime());
        for (int i = 1; i < MAX_NUM_NODES; i++)
            push(0x00010000 + i, lastHeard);
        TEST_ASSERT_TRUE(isFull());
    }

    // Fill to capacity with nodes last heard ageSecs ago.
    void fill(uint32_t ageSecs) { fillWithLastHeard(getTime() - ageSecs); }

    // Fill to one short of capacity, so admissions still find a free slot.
    void fillWithRoom()
    {
        meshNodes->clear();
        numMeshNodes = 0;
        push(0x0BADF00D, getTime());
        for (int i = 1; i < MAX_NUM_NODES - 1; i++)
            push(0x00010000 + i, getTime());
        TEST_ASSERT_FALSE(isFull());
    }

    // Admit a node the way updateFrom() does: create it (evicting when full), then stamp when we
    // heard it. Without the stamp the fresh arrival is itself the next eviction victim.
    void admit(NodeNum num, bool heardOnAir = true)
    {
        meshtastic_NodeInfoLite *n = getOrCreateMeshNode(num, heardOnAir);
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

// Drive `count` admissions through getOrCreateMeshNode, using a fresh block of node numbers each
// time so nothing is re-admitted out of the warm tier. On a full store every one of them evicts.
void churn(int count, uint32_t &seq, bool heardOnAir = true)
{
    for (int i = 0; i < count; i++)
        db->admit(FRESH_BASE + (seq++), heardOnAir);
}

// ---------------------------------------------------------------------------
// Probation band
// ---------------------------------------------------------------------------

// Fill the store, then admit `count` heard nodes: every one lands on probation.
void fillAndAdmit(int count, uint32_t &seq)
{
    db->fill(60);
    churn(count, seq);
}

// Promote `count` probation entries; each one evicts a resident once the band is at quota, which
// is what the churn ring counts. Returns the last node number promoted.
void promoteMany(int count, uint32_t firstSeq)
{
    for (int i = 0; i < count; i++)
        db->promote(FRESH_BASE + firstSeq + i);
}

// A heard node admitted to a full store is on probation; residents are untouched once the band exists.
void test_probation_heardAdmissionToFullStoreIsProbation(void)
{
    uint32_t seq = 100;
    fillAndAdmit(1, seq);

    TEST_ASSERT_TRUE_MESSAGE(db->onProbation(FRESH_BASE + 100), "a node heard on a full store starts on probation");
    TEST_ASSERT_EQUAL(1, db->probationCount());
}

// The band grows to its quota by displacing residents, then churns only within itself.
void test_probation_admissionsEvictProbationOnceBandIsFull(void)
{
    uint32_t seq = 200;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    const int residentsAtQuota = db->residents();
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS, db->probationCount());

    churn(NODEDB_PROBATION_SLOTS, seq); // a second band's worth

    TEST_ASSERT_EQUAL_MESSAGE(residentsAtQuota, db->residents(), "admissions past quota must not touch residents");
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS, db->probationCount());
    TEST_ASSERT_NULL_MESSAGE(db->getMeshNode(FRESH_BASE + 200), "the oldest probation entry is the one that went");
}

// A store with room admits residents, exactly as before the band existed.
void test_probation_storeWithRoomAdmitsResidents(void)
{
    uint32_t seq = 300;
    db->fillWithRoom();

    db->admit(FRESH_BASE + (seq++));

    TEST_ASSERT_FALSE_MESSAGE(db->onProbation(FRESH_BASE + 300), "no eviction, no probation");
}

// Contact imports and admin blocks are not heard packets and become residents directly.
void test_probation_notHeardOnAirAdmitsResident(void)
{
    uint32_t seq = 400;
    db->fill(60);

    churn(1, seq, /*heardOnAir=*/false);

    TEST_ASSERT_FALSE(db->onProbation(FRESH_BASE + 400));
}

// Heard again after the gap: promoted. Heard again inside it: still on probation.
void test_probation_promotesOnGapNotOnBurst(void)
{
    const NodeNum node = 0x66000001;
    const uint32_t t0 = 1700000000;
    db->fill(60);

    db->hear(node, t0);
    TEST_ASSERT_TRUE(db->onProbation(node));

    db->hear(node, t0 + db->probationGapSecs() - 1);
    TEST_ASSERT_TRUE_MESSAGE(db->onProbation(node), "a second packet inside the gap is a burst, not recurrence");

    db->hear(node, t0 + db->probationGapSecs() - 1 + db->probationGapSecs());
    TEST_ASSERT_FALSE_MESSAGE(db->onProbation(node), "a second packet after the gap promotes");
}

// A packet addressed to us promotes regardless of timing: the sender already knows us.
void test_probation_promotesWhenPacketAddressedToUs(void)
{
    const NodeNum node = 0x66000002;
    const uint32_t t0 = 1700000000;
    db->fill(60);

    db->hear(node, t0);
    db->hear(node, t0 + 1, /*toUs=*/true);

    TEST_ASSERT_FALSE(db->onProbation(node));
}

// Favourite / ignore / verify are the user vouching for the node.
void test_probation_protectedFlagPromotes(void)
{
    uint32_t seq = 500;
    fillAndAdmit(1, seq);
    TEST_ASSERT_TRUE(db->onProbation(FRESH_BASE + 500));

    TEST_ASSERT_TRUE(db->setProtectedFlag(db->getMeshNode(FRESH_BASE + 500), NODEINFO_BITFIELD_IS_FAVORITE_MASK, true));

    TEST_ASSERT_FALSE(db->onProbation(FRESH_BASE + 500));
}

// The gap halves with measured residency and never drops below the floor.
void test_probation_gapTracksResidency(void)
{
    uint32_t seq = 600;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_GAP_MAX_SECS, db->probationGapSecs());

    // Age the band by 20 s, then push it out: every eviction reports a 20 s stay.
    for (int i = 0; i < NODEDB_PROBATION_SLOTS; i++)
        db->getMeshNode(FRESH_BASE + 600 + i)->last_heard = getTime() - 20;
    churn(NODEDB_PROBATION_SLOTS * 4, seq);

    TEST_ASSERT_EQUAL_MESSAGE(NODEDB_PROBATION_GAP_MIN_SECS, db->probationGapSecs(),
                              "a band that turns over in seconds must sit at the floor, not collapse");
}

// ---------------------------------------------------------------------------
// Resident cap
// ---------------------------------------------------------------------------

// Promotions past the resident cap evict residents, one each, and never a probation entry.
void test_promotion_evictsResidentPastCap(void)
{
    uint32_t seq = 2000;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    const int residentsAtQuota = db->residents();

    promoteMany(3, 2000);

    TEST_ASSERT_EQUAL_MESSAGE(3, db->residentsEvicted, "each promotion past the cap evicts one resident");
    TEST_ASSERT_EQUAL_MESSAGE(residentsAtQuota, db->residents(), "the resident count holds at the cap");
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS - 3, db->probationCount());
}

// Below the cap a promotion evicts nobody.
void test_promotion_evictsNobodyBelowCap(void)
{
    uint32_t seq = 1100;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    // The user forgets a few residents; count down from the top, filling the band already evicted
    // the lowest-numbered ones.
    for (int i = 0; i < 3; i++)
        db->removeNodeByNum(0x00010000 + MAX_NUM_NODES - 1 - i);
    TEST_ASSERT_FALSE(db->isFull());

    promoteMany(3, 1100);

    TEST_ASSERT_EQUAL_MESSAGE(0, db->residentsEvicted, "a promotion into a band with room must not evict");
}

// The cap reads uptime through evictionRecency(): on a boot whose clock is never trusted every
// entry carries last_heard == 0, and the band must still work.
void test_promotion_worksWithClockNeverTrusted(void)
{
    uint32_t seq = 6000;
    db->fillWithLastHeard(0);
    churn(NODEDB_PROBATION_SLOTS, seq);
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS, db->probationCount());

    promoteMany(2, 6000);

    TEST_ASSERT_EQUAL(2, db->residentsEvicted);
    TEST_ASSERT_FALSE(db->onProbation(FRESH_BASE + 6000));
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

// A requester heard once on a full store is on probation: defer the reply to our scheduled
// broadcast. The request itself promotes it (it is addressed to us), so the next one is answered.
void test_reply_deferredWhileRequesterOnProbation(void)
{
    const uint32_t t0 = 1700000000;
    db->fill(60);
    db->hear(REQUESTER, t0, /*toUs=*/false); // heard once: on probation
    TEST_ASSERT_TRUE(db->onProbation(REQUESTER));

    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    shim.currentRequest = &req;

    TEST_ASSERT_NULL_MESSAGE(shim.allocReply(), "a probation requester must be deferred");
    TEST_ASSERT_TRUE(shim.ignoreRequest);

    // The request reaches updateFrom() after the modules: addressed to us, it promotes.
    db->hear(REQUESTER, t0 + 1, /*toUs=*/true);
    TEST_ASSERT_FALSE(db->onProbation(REQUESTER));
    shim.ignoreRequest = false;
    meshtastic_MeshPacket *reply = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "once promoted, the requester is answered");
    packetPool.release(reply);
}

// A resident on a full store is answered as before; the band only defers newcomers.
void test_reply_allowedForResidentOnAFullStore(void)
{
    db->fill(60);
    db->admit(REQUESTER, /*heardOnAir=*/false); // resident
    TEST_ASSERT_FALSE(db->onProbation(REQUESTER));

    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    shim.currentRequest = &req;

    meshtastic_MeshPacket *reply = shim.allocReply();
    TEST_ASSERT_NOT_NULL(reply);
    packetPool.release(reply);
}

// Our own scheduled broadcast is not a reply and must never be caught by the reply policy.
void test_periodicBroadcast_notSuppressed(void)
{
    uint32_t seq = 5000;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);

    NodeInfoStormShim shim;
    shim.currentRequest = nullptr; // not a reply - this is our own periodic send

    meshtastic_MeshPacket *p = shim.allocReply();
    TEST_ASSERT_NOT_NULL_MESSAGE(p, "our scheduled NodeInfo broadcast must not be suppressed");
    packetPool.release(p);
}

} // namespace

void setUp(void)
{
    db->resetProbation();
    db->residentsEvicted = 0;
}

void tearDown(void)
{
    NodeInfoStormShim::currentRequest = nullptr;
}

PROBATION_TEST_ENTRY void setup()
{
    initializeTestEnvironment();

    db = new NodeDBTestShim();
    nodeDB = db;
    router = new StormRouter();
    airTime = new AirTime();

    UNITY_BEGIN();

    printf("\n=== Probation band ===\n");
    RUN_TEST(test_probation_heardAdmissionToFullStoreIsProbation);
    RUN_TEST(test_probation_admissionsEvictProbationOnceBandIsFull);
    RUN_TEST(test_probation_storeWithRoomAdmitsResidents);
    RUN_TEST(test_probation_notHeardOnAirAdmitsResident);
    RUN_TEST(test_probation_promotesOnGapNotOnBurst);
    RUN_TEST(test_probation_promotesWhenPacketAddressedToUs);
    RUN_TEST(test_probation_protectedFlagPromotes);
    RUN_TEST(test_probation_gapTracksResidency);

    printf("\n=== Resident cap ===\n");
    RUN_TEST(test_promotion_evictsResidentPastCap);
    RUN_TEST(test_promotion_evictsNobodyBelowCap);
    RUN_TEST(test_promotion_worksWithClockNeverTrusted);

    printf("\n=== NodeInfo reply policy ===\n");
    RUN_TEST(test_reply_refusedForBroadcastRequest);
    RUN_TEST(test_reply_allowedForUnicastRequest);
    RUN_TEST(test_reply_deferredWhileRequesterOnProbation);
    RUN_TEST(test_reply_allowedForResidentOnAFullStore);
    RUN_TEST(test_periodicBroadcast_notSuppressed);

    exit(UNITY_END());
}

PROBATION_TEST_ENTRY void loop() {}
