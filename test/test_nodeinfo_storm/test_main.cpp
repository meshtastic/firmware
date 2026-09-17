// NodeInfo storm suppression: the NodeDB probation band (src/mesh/NodeDB.cpp), the greeting gate
// in src/mesh/MeshService.cpp and the reply policy in src/modules/NodeInfoModule.cpp that read it.
//
// Contract: on a full store a node heard once is admitted on probation, evicted ahead of every
// resident, not greeted and not answered; it becomes a resident (greeted, answered) only when
// heard again after probationGapSecs() (never because it addressed us); a promotion frees no slot,
// so the resident tier is capped at MAX_NUM_NODES - NODEDB_PROBATION_SLOTS by admission. Before this (develop @ 3468af94a) every
// admission to a full store evicted a real resident - NYC MediumSlow replay: 28.5 resident evictions/h at ~2 % channel
// utilisation - and each evicted-then-reheard node was greeted again and answered again, so a
// full store generated more NodeInfo traffic than one with room. A want_response broadcast was
// answered by every listener (one packet -> N replies), and its 12 h dedup entry then refused the
// same node's unicast request. None of that may return.
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
#include "mesh/TransmitHistory.h"
#include "modules/NodeInfoModule.h"
#include "support/MockMeshService.h"
#include <cstdio>
#include <cstring>

// Global scope so it matches `friend class NodeDBTestShim` in NodeDB.h - the probation helpers
// and the residency EMA are private.
class NodeDBTestShim : public NodeDB
{
  public:
    void resetProbation()
    {
        probationResidencyEmaSecs = 2 * NODEDB_PROBATION_GAP_MAX_SECS;
        for (auto &p : promotedAt)
            p = {};
    }
    bool inGrace(NodeNum num) const { return inPromotionGrace(num); }
    int residentsEvicted = 0; // counted by the shim's promote(), see below

    int probationCount() const { return scanForEviction().probationCount; }
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

    bool inWarm(NodeNum num) const { return warmStore.contains(num); }
    // The keyed resident the plain-oldest rule would take: fill() gives them one last_heard, and a
    // recency tie keeps the earliest index. Read after keyAllResidents(), never assumed by number -
    // filling the band has already evicted the lowest-numbered residents.
    NodeNum oldestKeyedResident() const
    {
        for (int i = 1; i < numMeshNodes; i++)
            if (meshNodes->at(i).public_key.size == 32 && !nodeInfoLiteIsOnProbation(&meshNodes->at(i)))
                return meshNodes->at(i).num;
        return 0;
    }
    // Give every resident a key, so a key-less promotion is the only "boring" candidate the victim
    // rule can pick - the shape a real mesh has, where most residents have sent a NodeInfo.
    void keyAllResidents()
    {
        for (int i = 1; i < numMeshNodes; i++)
            if (!nodeInfoLiteIsOnProbation(&meshNodes->at(i)))
                giveKey(meshNodes->at(i).num);
    }
    void armThrottle(bool armed) { lastFullEvictionMs = armed ? Time::getMillis() : Time::getMillis() - 3000; }
    void giveKey(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        n->public_key.size = 32;
        memset(n->public_key.bytes, 0x5A, 32);
    }

    // Deliver a decoded packet the way the radio path does, so updateFrom() runs its probation
    // logic. rxTime is an epoch; toUs addresses the packet to our own node number.
    // throttled=true leaves the 2 s full-store admission throttle armed instead of stepping past it.
    void hear(NodeNum from, uint32_t rxTime, bool toUs = false, bool throttled = false)
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
        armThrottle(throttled);
        updateFrom(mp);
    }

    // Fill to capacity with favourites only - the pre-cap legacy shape getOrCreateMeshNode must refuse.
    void fillAllProtected()
    {
        fill(60);
        for (int i = 1; i < numMeshNodes; i++)
            nodeInfoLiteSetBit(&meshNodes->at(i), NODEINFO_BITFIELD_IS_FAVORITE_MASK, true);
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
        sent.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override { packetPool.release(p); }
    std::vector<meshtastic_MeshPacket> sent;
};
StormRouter *stormRouter = nullptr;

class StormRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t, bool = false) override { return 0; }
};

class NodeInfoStormShim : public NodeInfoModule
{
  public:
    using MeshModule::currentRequest;
    using MeshModule::ignoreRequest;
    using NodeInfoModule::allocReply;
    using NodeInfoModule::handleReceivedProtobuf;
    // The module pass as the router runs it: record the requester, then decide the reply.
    meshtastic_MeshPacket *receiveAndReply(meshtastic_MeshPacket &req)
    {
        meshtastic_User u = meshtastic_User_init_zero;
        snprintf(u.short_name, sizeof(u.short_name), "REQ");
        handleReceivedProtobuf(req, &u);
        currentRequest = &req;
        ignoreRequest = false;
        return allocReply();
    }
};

} // namespace

// handleFromRadio() is private; MeshService befriends this exact name under PIO_UNIT_TESTING.
class MeshServicePhoneDeliveryTest
{
  public:
    static void deliver(const meshtastic_MeshPacket &p) { service->handleFromRadio(&p); }
};

namespace
{

// Run a decoded text broadcast from `from` through the radio path (updateFrom, then the greeting
// gate) and report whether we sent it our NodeInfo. Drains the phone queue so nothing leaks.
bool heardAndGreeted(NodeNum from, uint32_t rxTime, bool throttled = false)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = NODENUM_BROADCAST;
    mp.id = 0x2000 + from;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    mp.has_rx_time = true;
    mp.rx_time = rxTime;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    db->armThrottle(throttled);

    stormRouter->sent.clear();
    MeshServicePhoneDeliveryTest::deliver(mp);
    while (meshtastic_MeshPacket *q = service->getForPhone())
        service->releaseToPool(q);

    for (const auto &s : stormRouter->sent)
        if (s.to == from && s.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            s.decoded.portnum == meshtastic_PortNum_NODEINFO_APP)
            return true;
    return false;
}

// Forget our last NodeInfo transmission, so the next greeting or reply is not refused by
// allocReply()'s TX throttle. Guarded: TransmitHistory::getInstance() would *create* the global,
// and a suite that never sends for real is meant to see the throttle only where a case arms it.
void clearNodeInfoThrottle()
{
    if (transmitHistory)
        transmitHistory->clear();
}

void giveUser(NodeNum num)
{
    meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
}

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

// The destination is a plaintext header field: a stranger addressing us is not evidence of anything,
// and letting it promote would turn a rotating-from unicast flood into one resident lost per packet.
void test_probation_packetAddressedToUsDoesNotPromote(void)
{
    const NodeNum node = 0x66000002;
    const uint32_t t0 = 1700000000;
    db->fill(60);

    db->hear(node, t0);
    db->hear(node, t0 + 1, /*toUs=*/true);
    TEST_ASSERT_TRUE_MESSAGE(db->onProbation(node), "addressing us inside the gap must not promote");

    db->hear(node, t0 + 1 + db->probationGapSecs(), /*toUs=*/true);
    TEST_ASSERT_FALSE_MESSAGE(db->onProbation(node), "the gap rule still applies to packets addressed to us");
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

// A promotion frees nothing; the store stays full and the resident count sits over the cap until
// admissions refill the band, one resident eviction per newcomer. Never two, never a stranger let in.
void test_promotion_evictsNothingUntilTheBandRefills(void)
{
    uint32_t seq = 2000;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    const int residentsAtQuota = db->residents();

    promoteMany(3, 2000);
    TEST_ASSERT_EQUAL_MESSAGE(0, db->residentsEvicted, "a promotion evicts nobody");
    TEST_ASSERT_EQUAL(residentsAtQuota + 3, db->residents());
    TEST_ASSERT_TRUE_MESSAGE(db->isFull(), "a promotion leaves no free slot for a stranger");

    churn(3, seq);
    TEST_ASSERT_EQUAL_MESSAGE(residentsAtQuota, db->residents(), "each refill admission evicts one resident");
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS, db->probationCount());
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_TRUE_MESSAGE(db->onProbation(FRESH_BASE + seq - 1 - i), "every refill newcomer is on probation");
}

// add_contact sets the verified bit on a key-less entry whenever the client says so; the key-less
// ("boring") victim rule must skip it like the oldest-node rule does, or it is the first to go.
void test_eviction_skipsKeylessVerifiedResident(void)
{
    const NodeNum verified = 0x00010001; // the oldest resident fill() creates
    db->fill(60);
    meshtastic_NodeInfoLite *n = db->getMeshNode(verified);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL(0, n->public_key.size);
    TEST_ASSERT_TRUE(db->setProtectedFlag(n, NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true));

    db->admit(FRESH_BASE + 1);

    TEST_ASSERT_NOT_NULL_MESSAGE(db->getMeshNode(verified), "a key-less verified resident is never the eviction victim");
    TEST_ASSERT_NOT_NULL(db->getMeshNode(FRESH_BASE + 1));
}

// A promotion has no key yet, so the key-less-first rule would evict it at the next admission,
// before a greeting or its own NodeInfo could name it. Inside the grace a keyed resident goes instead.
void test_eviction_promotedResidentIsSparedTheKeylessPick(void)
{
    uint32_t seq = 1200;
    Time::setTestMillis(10 * 60 * 1000);
    fillAndAdmit(1, seq);
    db->keyAllResidents();
    db->promote(FRESH_BASE + 1200);
    TEST_ASSERT_EQUAL(0, db->getMeshNode(FRESH_BASE + 1200)->public_key.size);
    TEST_ASSERT_TRUE(db->inGrace(FRESH_BASE + 1200));
    const NodeNum oldestKeyed = db->oldestKeyedResident();
    TEST_ASSERT_NOT_EQUAL(0, oldestKeyed);

    db->admit(FRESH_BASE + 1201);

    TEST_ASSERT_NOT_NULL_MESSAGE(db->getMeshNode(FRESH_BASE + 1200), "a promotion inside its grace is not the key-less victim");
    TEST_ASSERT_NULL_MESSAGE(db->getMeshNode(oldestKeyed), "the oldest keyed resident goes instead");
    Time::useRealClock();
}

// The grace is a window, not a permanent exemption: past it the promotion is an ordinary key-less
// resident. Nothing named it, so there is nothing left to protect.
void test_eviction_promotionGraceExpires(void)
{
    uint32_t seq = 1300;
    Time::setTestMillis(10 * 60 * 1000);
    fillAndAdmit(1, seq);
    db->keyAllResidents();
    db->promote(FRESH_BASE + 1300);
    const NodeNum oldestKeyed = db->oldestKeyedResident();
    TEST_ASSERT_NOT_EQUAL(0, oldestKeyed);

    Time::advanceTestMillis((NODEDB_PROMOTION_GRACE_SECS + 60) * 1000UL);
    TEST_ASSERT_FALSE(db->inGrace(FRESH_BASE + 1300));
    db->admit(FRESH_BASE + 1301);

    TEST_ASSERT_NULL_MESSAGE(db->getMeshNode(FRESH_BASE + 1300), "past the grace the key-less promotion is the victim again");
    TEST_ASSERT_NOT_NULL_MESSAGE(db->getMeshNode(oldestKeyed), "and the keyed resident it was shielding stays");
    Time::useRealClock();
}

// Grace is a preference, not immunity: when it is the only entry the victim rule may take, it goes
// and the admission still succeeds. A full store must never wedge.
void test_eviction_promotionGraceYieldsWhenItIsTheOnlyVictim(void)
{
    uint32_t seq = 1400;
    Time::setTestMillis(10 * 60 * 1000);
    fillAndAdmit(1, seq);
    db->promote(FRESH_BASE + 1400);
    for (int i = 1; i < db->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *n = db->getMeshNodeByIndex(i);
        if (n->num != FRESH_BASE + 1400)
            TEST_ASSERT_TRUE(db->setProtectedFlag(n, NODEINFO_BITFIELD_IS_FAVORITE_MASK, true));
    }

    db->admit(FRESH_BASE + 1401);

    TEST_ASSERT_NULL_MESSAGE(db->getMeshNode(FRESH_BASE + 1400), "the graced promotion yields rather than refuse the admission");
    TEST_ASSERT_NOT_NULL(db->getMeshNode(FRESH_BASE + 1401));
    Time::useRealClock();
}

// With room in the store a promotion evicts nobody and the next admission evicts nobody either.
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

    const int before = db->getNumMeshNodes();
    db->admit(FRESH_BASE + 1150);
    TEST_ASSERT_EQUAL_MESSAGE(before + 1, db->getNumMeshNodes(), "a store with room admits without evicting");
    TEST_ASSERT_FALSE_MESSAGE(db->onProbation(FRESH_BASE + 1150), "a store with room admits residents");
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
    TEST_ASSERT_FALSE(db->onProbation(FRESH_BASE + 6000));

    // The refill admissions must still find a resident victim with every last_heard at 0.
    churn(2, seq);
    TEST_ASSERT_EQUAL(NODEDB_PROBATION_SLOTS, db->probationCount());
    TEST_ASSERT_NOT_NULL_MESSAGE(db->getMeshNode(FRESH_BASE + 6000), "the promoted node is not the refill victim");
}

// ---------------------------------------------------------------------------
// Admission edges
// ---------------------------------------------------------------------------

// Inside 2 s of the last full-store eviction a newcomer gets no entry at all (mp.from is
// unauthenticated; without this an invented number per packet churns the store at packet rate).
void test_admission_deferredInsideTheFullStoreThrottle(void)
{
    const uint32_t t0 = 1700000000;
    db->fill(60);
    db->hear(0x61000001, t0);
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x61000001));

    db->hear(0x61000002, t0 + 1, /*toUs=*/false, /*throttled=*/true);
    TEST_ASSERT_NULL_MESSAGE(db->getMeshNode(0x61000002), "admission inside the 2 s throttle must be deferred");
}

// The legacy shape - a full store of protected nodes and no band - is refused rather than overrun.
void test_admission_refusedWhenEveryResidentIsProtected(void)
{
    db->fillAllProtected();
    TEST_ASSERT_NULL(db->getOrCreateMeshNode(0x61000003));
    TEST_ASSERT_EQUAL(MAX_NUM_NODES, db->getNumMeshNodes());
}

// A NodeInfo broadcast is just another packet: on a full store the sender joins the band, name and all.
void test_admission_nodeInfoOnFullStoreIsProbation(void)
{
    db->fill(60);
    meshtastic_User u = meshtastic_User_init_zero;
    snprintf(u.short_name, sizeof(u.short_name), "NEW");
    db->updateUser(0x61000004, u);

    TEST_ASSERT_TRUE(db->onProbation(0x61000004));
    TEST_ASSERT_TRUE(nodeInfoLiteHasUser(db->getMeshNode(0x61000004)));
}

// After a promotion the store is still full: the next newcomer joins the band and evicts one
// resident. (Promotion used to evict, leaving a slot the next stranger walked into as a resident.)
void test_admission_afterPromotionJoinsTheBand(void)
{
    uint32_t seq = 6000;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    const int residentsAtCap = db->residents();
    db->promote(FRESH_BASE + 6000);
    TEST_ASSERT_EQUAL(residentsAtCap + 1, db->residents());
    TEST_ASSERT_TRUE(db->isFull());

    db->admit(0x61000005);
    TEST_ASSERT_TRUE_MESSAGE(db->onProbation(0x61000005), "the newcomer after a promotion is on probation");
    TEST_ASSERT_EQUAL_MESSAGE(residentsAtCap, db->residents(), "and its admission evicts one resident, not two");
}

// ---------------------------------------------------------------------------
// Eviction destination
// ---------------------------------------------------------------------------

// A resident evicted for admission keeps its identity in the warm tier; a key-less probation
// entry is dropped outright; a keyed one is kept.
void test_eviction_warmTierKeepsResidentsAndKeyedProbationOnly(void)
{
    uint32_t seq = 6100;
    const NodeNum oldestResident = 0x00010001;
    db->fill(60);
    db->admit(FRESH_BASE + (seq++)); // evicts the oldest resident
    TEST_ASSERT_NULL(db->getMeshNode(oldestResident));
    TEST_ASSERT_TRUE_MESSAGE(db->inWarm(oldestResident), "an evicted resident goes to the warm tier");

    churn(NODEDB_PROBATION_SLOTS - 1, seq); // band at quota; the first probation entry is the oldest
    const NodeNum keyless = FRESH_BASE + 6100, keyed = FRESH_BASE + 6101;
    db->giveKey(keyed);
    churn(2, seq); // evicts keyless, then keyed
    TEST_ASSERT_NULL(db->getMeshNode(keyless));
    TEST_ASSERT_FALSE_MESSAGE(db->inWarm(keyless), "a key-less probation entry is not worth a warm slot");
    TEST_ASSERT_NULL(db->getMeshNode(keyed));
    TEST_ASSERT_TRUE_MESSAGE(db->inWarm(keyed), "a keyed probation entry keeps its key in the warm tier");
}

// Re-admission from the warm tier restores the key but not residency: on a full store it is on probation.
void test_eviction_rehydratedNodeIsStillOnProbation(void)
{
    uint32_t seq = 6200;
    const uint32_t t0 = 1700000000;
    const NodeNum keyed = FRESH_BASE + 6200; // the first probation entry, hence the band's next victim
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    db->giveKey(keyed);
    churn(1, seq);
    TEST_ASSERT_NULL(db->getMeshNode(keyed));
    TEST_ASSERT_TRUE(db->inWarm(keyed));

    db->hear(keyed, t0);
    const meshtastic_NodeInfoLite *n = db->getMeshNode(keyed);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_MESSAGE(32, n->public_key.size, "the warm tier hands the key back");
    TEST_ASSERT_FALSE(db->inWarm(keyed));
    TEST_ASSERT_TRUE_MESSAGE(db->onProbation(keyed), "warm data does not promote");
}

// The warm tier carries the greeting mark in a spare metadata bit, so a re-admitted node - keyed
// but nameless, since the warm tier keeps no name - is not asked for its NodeInfo a second time.
void test_eviction_warmTierCarriesTheGreetingMark(void)
{
    uint32_t seq = 6300;
    const uint32_t t0 = 1700000000;
    const NodeNum marked = FRESH_BASE + 6300; // the band's next victim, then the one after it
    const NodeNum unmarked = FRESH_BASE + 6301;
    fillAndAdmit(NODEDB_PROBATION_SLOTS, seq);
    db->giveKey(marked);
    db->giveKey(unmarked);
    nodeInfoLiteSetBit(db->getMeshNode(marked), NODEINFO_BITFIELD_HAS_BEEN_GREETED_MASK, true);

    churn(2, seq); // evicts the two oldest probation entries, both keyed so both are kept warm
    TEST_ASSERT_TRUE(db->inWarm(marked));
    TEST_ASSERT_TRUE(db->inWarm(unmarked));

    db->hear(marked, t0);
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasBeenGreeted(db->getMeshNode(marked)),
                             "re-admission restores the ask we already sent");
    db->hear(unmarked, t0);
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasBeenGreeted(db->getMeshNode(unmarked)),
                              "and a node we never asked comes back askable");
}

// ---------------------------------------------------------------------------
// Greeting gate (MeshService::handleFromRadio)
// ---------------------------------------------------------------------------

// A resident without a user record is greeted - with room, and on a full store (the old !isFull()
// gate is gone).
void test_greeting_residentWithoutUserIsGreeted(void)
{
    const uint32_t t0 = 1700000000;
    db->fillWithRoom();
    TEST_ASSERT_TRUE_MESSAGE(heardAndGreeted(0x62000001, t0), "new node on a store with room is greeted");

    clearNodeInfoThrottle(); // the greeting above counts as a NodeInfo transmission once armed
    db->fill(60);
    db->admit(0x62000002, /*heardOnAir=*/false); // a contact import: resident, no user record yet
    meshtastic_NodeInfoLite *n = db->getMeshNode(0x62000002);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_USER_MASK, false);
    TEST_ASSERT_TRUE_MESSAGE(heardAndGreeted(0x62000002, t0), "a resident on a full store is greeted");
}

// One ask per residency. The mark lives in the bitfield and is only read while the node has no user
// record, so the states are: nameless -> greeted, no answer -> named by its NodeInfo.
void test_greeting_onlyOnceWhileStillNameless(void)
{
    const uint32_t t0 = 1700000000;
    db->fillWithRoom();
    TEST_ASSERT_TRUE(heardAndGreeted(0x62000010, t0));
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasBeenGreeted(db->getMeshNode(0x62000010)), "the ask that went out is marked");

    TEST_ASSERT_FALSE_MESSAGE(heardAndGreeted(0x62000010, t0 + 30), "a node that did not answer is not asked twice");
}

// The mark is not sticky state: the NodeInfo we asked for clears it, so a node whose record is
// later dropped can be asked again.
void test_greeting_markClearedByTheNodeInfoItAskedFor(void)
{
    const uint32_t t0 = 1700000000;
    db->fillWithRoom();
    TEST_ASSERT_TRUE(heardAndGreeted(0x62000011, t0));
    TEST_ASSERT_TRUE(nodeInfoLiteHasBeenGreeted(db->getMeshNode(0x62000011)));

    meshtastic_User u = meshtastic_User_init_zero;
    snprintf(u.short_name, sizeof(u.short_name), "ANS");
    db->updateUser(0x62000011, u);

    const meshtastic_NodeInfoLite *n = db->getMeshNode(0x62000011);
    TEST_ASSERT_TRUE(nodeInfoLiteHasUser(n));
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasBeenGreeted(n), "the answer clears the mark");
}

// A greeting refused by our own NodeInfo TX throttle never went out, so it must not spend the
// node's one ask - the throttle is 10 min scaled by online count, the commonest refusal there is.
void test_greeting_markNotSpentWhenTheSendIsRefused(void)
{
    const uint32_t t0 = 1700000000;
    db->fillWithRoom();
    // setLastSentToMesh(), not the persisted setters: allocReply() reads the runtime millis map, the
    // only one both the filesystem and the stub build of TransmitHistory keep.
    TransmitHistory::getInstance()->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);

    TEST_ASSERT_FALSE_MESSAGE(heardAndGreeted(0x62000012, t0), "inside our NodeInfo throttle nothing is sent");
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasBeenGreeted(db->getMeshNode(0x62000012)), "a refused ask leaves the node askable");

    clearNodeInfoThrottle();
    TEST_ASSERT_TRUE_MESSAGE(heardAndGreeted(0x62000012, t0 + 30), "and it is asked once the throttle clears");
}

// A node whose user record we hold is never greeted.
void test_greeting_knownUserIsNotGreeted(void)
{
    const uint32_t t0 = 1700000000;
    db->fillWithRoom();
    db->hear(0x62000003, t0);
    giveUser(0x62000003);
    TEST_ASSERT_FALSE(heardAndGreeted(0x62000003, t0 + 1));
}

// Neither a probation entry nor a deferred newcomer is greeted; the packet that promotes one is.
void test_greeting_probationAndDeferredAreNotGreeted(void)
{
    const uint32_t t0 = 1700000000;
    db->fill(60);
    TEST_ASSERT_FALSE_MESSAGE(heardAndGreeted(0x62000004, t0), "first hearing on a full store: probation, no greeting");
    TEST_ASSERT_TRUE(db->onProbation(0x62000004));
    TEST_ASSERT_FALSE_MESSAGE(heardAndGreeted(0x62000005, t0 + 1, /*throttled=*/true), "deferred: no entry, no greeting");
    TEST_ASSERT_NULL(db->getMeshNode(0x62000005));

    TEST_ASSERT_TRUE_MESSAGE(heardAndGreeted(0x62000004, t0 + 1 + db->probationGapSecs()),
                             "heard again after the gap: promoted, and greeted on that packet");
    TEST_ASSERT_FALSE(db->onProbation(0x62000004));
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
// broadcast. Asking again inside the gap changes nothing; asking again after it is answered.
void test_reply_deferredWhileRequesterOnProbation(void)
{
    const uint32_t t0 = 1700000000;
    db->fill(60);
    db->hear(REQUESTER, t0, /*toUs=*/false); // heard once: on probation
    TEST_ASSERT_TRUE(db->onProbation(REQUESTER));

    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());

    // Through handleReceivedProtobuf(), so the 12 h dedup record is made and must be undone.
    TEST_ASSERT_NULL_MESSAGE(shim.receiveAndReply(req), "a probation requester must be deferred");
    TEST_ASSERT_TRUE(shim.ignoreRequest);

    // The request reaches updateFrom() after the modules: addressed to us, still inside the gap.
    db->hear(REQUESTER, t0 + 1, /*toUs=*/true);
    TEST_ASSERT_TRUE(db->onProbation(REQUESTER));
    TEST_ASSERT_NULL_MESSAGE(shim.receiveAndReply(req), "asking again inside the gap is still deferred");

    // Heard again after the gap: promoted, and the request that follows is answered.
    db->hear(REQUESTER, t0 + 1 + db->probationGapSecs(), /*toUs=*/true);
    TEST_ASSERT_FALSE(db->onProbation(REQUESTER));
    meshtastic_MeshPacket *reply = shim.receiveAndReply(req);
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "once promoted, the requester's next request is answered");
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

// A resident asking twice inside the 12 h window is answered once.
void test_reply_refusedWithinTwelveHoursOfTheLast(void)
{
    db->fill(60);
    db->admit(REQUESTER, /*heardOnAir=*/false);

    NodeInfoStormShim shim;
    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    meshtastic_MeshPacket *reply = shim.receiveAndReply(req);
    TEST_ASSERT_NOT_NULL(reply);
    packetPool.release(reply);

    TEST_ASSERT_NULL_MESSAGE(shim.receiveAndReply(req), "a second unicast request inside 12 h is refused");
}

// A refused broadcast request is not recorded, so the same node's unicast request is still answered.
void test_reply_broadcastRequestDoesNotArmTheDedup(void)
{
    db->fill(60);
    db->admit(REQUESTER, /*heardOnAir=*/false);

    NodeInfoStormShim shim;
    meshtastic_MeshPacket bcast = makeNodeInfoRequest(NODENUM_BROADCAST);
    TEST_ASSERT_NULL(shim.receiveAndReply(bcast));

    meshtastic_MeshPacket req = makeNodeInfoRequest(db->getNodeNum());
    meshtastic_MeshPacket *reply = shim.receiveAndReply(req);
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "the broadcast refusal must not suppress the unicast request");
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
    // A case that armed our NodeInfo TX throttle must not leave it armed: an aborted assertion skips
    // the case's own cleanup, and every later greeting or reply would then be refused.
    clearNodeInfoThrottle();
}

PROBATION_TEST_ENTRY void setup()
{
    initializeTestEnvironment();

    db = new NodeDBTestShim();
    nodeDB = db;
    stormRouter = new StormRouter();
    stormRouter->addInterface(std::unique_ptr<RadioInterface>(new StormRadio()));
    router = stormRouter;
    airTime = new AirTime();
    service = new MockMeshService();
    nodeInfoModule = new NodeInfoStormShim(); // the greeting gate needs the module the service calls

    UNITY_BEGIN();

    printf("\n=== Probation band ===\n");
    RUN_TEST(test_probation_heardAdmissionToFullStoreIsProbation);
    RUN_TEST(test_probation_admissionsEvictProbationOnceBandIsFull);
    RUN_TEST(test_probation_storeWithRoomAdmitsResidents);
    RUN_TEST(test_probation_notHeardOnAirAdmitsResident);
    RUN_TEST(test_probation_promotesOnGapNotOnBurst);
    RUN_TEST(test_probation_packetAddressedToUsDoesNotPromote);
    RUN_TEST(test_probation_protectedFlagPromotes);
    RUN_TEST(test_probation_gapTracksResidency);

    printf("\n=== Admission edges ===\n");
    RUN_TEST(test_admission_deferredInsideTheFullStoreThrottle);
    RUN_TEST(test_admission_refusedWhenEveryResidentIsProtected);
    RUN_TEST(test_admission_nodeInfoOnFullStoreIsProbation);
    RUN_TEST(test_admission_afterPromotionJoinsTheBand);

    printf("\n=== Resident cap ===\n");
    RUN_TEST(test_promotion_evictsNothingUntilTheBandRefills);
    RUN_TEST(test_promotion_evictsNobodyBelowCap);
    RUN_TEST(test_eviction_skipsKeylessVerifiedResident);
    RUN_TEST(test_eviction_promotedResidentIsSparedTheKeylessPick);
    RUN_TEST(test_eviction_promotionGraceExpires);
    RUN_TEST(test_eviction_promotionGraceYieldsWhenItIsTheOnlyVictim);
    RUN_TEST(test_promotion_worksWithClockNeverTrusted);

    printf("\n=== Eviction destination ===\n");
    RUN_TEST(test_eviction_warmTierKeepsResidentsAndKeyedProbationOnly);
    RUN_TEST(test_eviction_rehydratedNodeIsStillOnProbation);

    printf("\n=== Greeting gate ===\n");
    RUN_TEST(test_eviction_warmTierCarriesTheGreetingMark);
    RUN_TEST(test_greeting_residentWithoutUserIsGreeted);
    RUN_TEST(test_greeting_onlyOnceWhileStillNameless);
    RUN_TEST(test_greeting_markClearedByTheNodeInfoItAskedFor);
    RUN_TEST(test_greeting_markNotSpentWhenTheSendIsRefused);
    RUN_TEST(test_greeting_knownUserIsNotGreeted);
    RUN_TEST(test_greeting_probationAndDeferredAreNotGreeted);

    printf("\n=== NodeInfo reply policy ===\n");
    RUN_TEST(test_reply_refusedForBroadcastRequest);
    RUN_TEST(test_reply_allowedForUnicastRequest);
    RUN_TEST(test_reply_deferredWhileRequesterOnProbation);
    RUN_TEST(test_reply_allowedForResidentOnAFullStore);
    RUN_TEST(test_reply_refusedWithinTwelveHoursOfTheLast);
    RUN_TEST(test_reply_broadcastRequestDoesNotArmTheDedup);
    RUN_TEST(test_periodicBroadcast_notSuppressed);

    exit(UNITY_END());
}

PROBATION_TEST_ENTRY void loop() {}
