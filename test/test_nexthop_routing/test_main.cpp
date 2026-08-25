// Unit tests for NextHop direct-message reliability mitigations (landed in meshtastic/firmware#10745):
//   M1 - NodeDB::resolveLastByte / resolveUniqueLastByte (ambiguity-aware last-byte resolution)
//   M2 - NextHopRouter::getNextHop strict-neighbor gate + Router::shouldDecrementHopLimit favorite check
//   M3 - NextHopRouter route-health freshness / failure decay
//
// Time handling: the route-health helpers take `now` as a parameter so the 30-minute TTL logic is
// pure and testable without a clock mock. getNextHop()/sinceLastSeen() use the real native clock;
// we back-date timestamps relative to it, and the unsigned-subtraction age math is rollover-safe.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/Default.h"
#include "mesh/NextHopRouter.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/ReliableRouter.h"
#include "modules/RoutingModule.h"
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <tuple>
#include <vector>

#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                          \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

static constexpr NodeNum kLocalNode = 0x11111111; // last byte 0x11
static constexpr NodeNum kRemoteNode = 0x22222222;

#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
static constexpr bool kEventPolicyEnabled = true;
#else
static constexpr bool kEventPolicyEnabled = false;
#endif

// ---------------------------------------------------------------------------
// MockNodeDB - inject nodes with controlled last byte, hop distance, age, role, favorite flag.
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    // ageSecs is how long ago we last heard the node; getTime() returns a large Unix timestamp on
    // native, so getTime()-ageSecs does not underflow for the ranges used here.
    void addNode(NodeNum num, uint8_t hopsAway, bool hasHops, uint32_t ageSecs,
                 meshtastic_Config_DeviceConfig_Role role = meshtastic_Config_DeviceConfig_Role_CLIENT, bool favorite = false,
                 bool ignored = false, uint8_t nextHop = NO_NEXT_HOP_PREFERENCE)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.has_hops_away = hasHops;
        node.hops_away = hopsAway;
        node.role = role;
        node.next_hop = nextHop;
        node.last_heard = getTime() - ageSecs;
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_IS_FAVORITE_MASK, favorite);
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_IS_IGNORED_MASK, ignored);
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

// ---------------------------------------------------------------------------
// Test shim - expose getNextHop and the route-health helpers; reset health between tests.
// Nulls cryptLock so the Router base can be (re)constructed (same pattern as test_mqtt MockRouter).
// ---------------------------------------------------------------------------
class NextHopRouterTestShim : public NextHopRouter
{
  public:
    NextHopRouterTestShim() : NextHopRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    using NextHopRouter::clearRouteHealth;
    using NextHopRouter::findRouteHealth;
    using NextHopRouter::getNextHop;
    using NextHopRouter::getOrAllocRouteHealth;
    using NextHopRouter::isRouteStale;
    using NextHopRouter::noteRouteFailure;
    using NextHopRouter::noteRouteLearned;
    using NextHopRouter::noteRouteSuccess;
    using NextHopRouter::perhapsRebroadcast;
    using NextHopRouter::relayOpaquePacket;
    using Router::shouldDecrementHopLimit; // protected in Router

    PendingPacket *trackForTest(const meshtastic_MeshPacket &packet, uint8_t totalAttempts)
    {
        auto *copy = packetPool.allocCopy(packet);
        TEST_ASSERT_NOT_NULL(copy);
        return startRetransmission(copy, totalAttempts);
    }

    PendingPacket *trackWithDefaultBudgetForTest(const meshtastic_MeshPacket &packet)
    {
        auto *copy = packetPool.allocCopy(packet);
        TEST_ASSERT_NOT_NULL(copy);
        return startRetransmission(copy);
    }

    bool stopForTest(NodeNum from, PacketId id) { return stopRetransmission(from, id); }

    meshtastic_MeshPacket *pendingPacketForTest(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        return entry ? entry->packet : nullptr;
    }

    void fireNextRetryForTest(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        TEST_ASSERT_NOT_NULL(entry);
        entry->nextTxMsec = 0;
        doRetransmissions();
    }

    void markOneRetryFiredForTest(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_GREATER_THAN_UINT8(0, entry->numRetransmissions);
        --entry->numRetransmissions;
    }

    bool filterViaFlooding(const meshtastic_MeshPacket *p) { return FloodingRouter::shouldFilterReceived(p); }
    bool filterViaNextHop(const meshtastic_MeshPacket *p) { return NextHopRouter::shouldFilterReceived(p); }

    void clearPendingForTest()
    {
        while (!pending.empty())
            stopRetransmission(pending.begin()->first);
    }

    void resetRouteHealthForTest()
    {
        for (auto &h : routeHealth)
            h = RouteHealth{};
    }
};

// Mirrors RadioLibInterface::send()'s NODENUM_BROADCAST_NO_LORA branch, which
// returns ERRNO_SHOULD_RELEASE without releasing the packet.
class MockRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sendCount++;
        lastHopLimit = p->hop_limit;
        lastHopStart = p->hop_start;
        sentNextHops.push_back(p->next_hop);
        if (declineAll || p->to == NODENUM_BROADCAST_NO_LORA)
            return ERRNO_SHOULD_RELEASE;

        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }

    bool cancelSending(NodeNum, PacketId) override
    {
        cancelCount++;
        return true;
    }

    int sendCount = 0;
    uint32_t cancelCount = 0;
    bool declineAll = false;
    uint8_t lastHopLimit = 0;
    uint8_t lastHopStart = 0;
    std::vector<uint8_t> sentNextHops;
};

class CaptureRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sentPackets.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }

    bool cancelSending(NodeNum from, PacketId id) override
    {
        (void)from;
        (void)id;
        cancelCount++;
        return false;
    }

    bool findInTxQueue(NodeNum from, PacketId id) override
    {
        (void)from;
        (void)id;
        return false;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }

    void reset()
    {
        sentPackets.clear();
        cancelCount = 0;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
    uint32_t cancelCount = 0;
};

class ReliableRouterTestShim : public ReliableRouter
{
  public:
    ReliableRouterTestShim() : ReliableRouter() {}

    size_t pendingCount() const { return pending.size(); }

    void seedRetry(const meshtastic_MeshPacket &p, uint8_t attempts)
    {
        auto *copy = packetPool.allocCopy(p);
        TEST_ASSERT_NOT_NULL(copy);
        startRetransmission(copy, attempts);
    }

    void makeRetryDue(NodeNum from, PacketId id)
    {
        PendingPacket *record = findPendingPacket(from, id);
        TEST_ASSERT_NOT_NULL(record);
        record->nextTxMsec = 0;
    }

    int32_t runDueRetries() { return doRetransmissions(); }
    void sniffForTest(const meshtastic_MeshPacket *p, const meshtastic_Routing *routing)
    {
        ReliableRouter::sniffReceived(p, routing);
    }

    void implicitAckForTest(const meshtastic_MeshPacket *p) { perhapsGenerateImplicitAckForOwnOverheard(p); }

    void clearPendingForTest()
    {
        while (!pending.empty())
            stopRetransmission(pending.begin()->first);
    }
};

class MockRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false) override
    {
        ackNaks.emplace_back(err, to, idFrom, chIndex, hopLimit, ackWantsAck);
    }

    std::list<std::tuple<meshtastic_Routing_Error, NodeNum, PacketId, ChannelIndex, uint8_t, bool>> ackNaks;
};

class ScopedAirTimeFixture
{
  public:
    ScopedAirTimeFixture() : previous(airTime) { airTime = &instance; }
    ~ScopedAirTimeFixture() { airTime = previous; }

  private:
    AirTime instance;
    AirTime *previous;
};

static meshtastic_MeshPacket makeRebroadcastCandidate(NodeNum to)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = kRemoteNode;
    p.to = to;
    p.id = 0x0BADF00D;
    p.hop_start = 3;
    p.hop_limit = 3;
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 8;
    return p;
}

static MockNodeDB *mockNodeDB = nullptr;
static NextHopRouterTestShim *shim = nullptr;
static ReliableRouterTestShim *reliableShim = nullptr;
static CaptureRadioInterface *nextHopRadio = nullptr;
static CaptureRadioInterface *reliableRadio = nullptr;
static MockRoutingModule *mockRoutingModule = nullptr;
static std::unique_ptr<ScopedAirTimeFixture> airTimeFixture;
static PacketId nextBehaviorPacketId = 0x70000000;

static MockRadioInterface *installMockIface()
{
    MockRadioInterface *mock = new MockRadioInterface();
    // addInterface replaces and destroys the suite's original capture interface.
    // Clear its borrowed pointer before the next Unity setUp() runs.
    nextHopRadio = nullptr;
    shim->addInterface(std::unique_ptr<RadioInterface>(mock));
    return mock;
}

static constexpr uint32_t TTL = NextHopRouter::ROUTE_TTL_MSEC;
static constexpr uint8_t THRESH = NextHopRouter::ROUTE_FAILURE_THRESHOLD;
static constexpr uint8_t HEALTH_MAX = NextHopRouter::ROUTE_HEALTH_MAX;

// Helper: a decoded packet whose hops-away is `hopsAway`, relayed by last byte `relay`.
static meshtastic_MeshPacket makeRelayedPacket(uint8_t relay, uint8_t hopsAway)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.relay_node = relay;
    p.hop_start = 4;
    p.hop_limit = 4 - hopsAway; // getHopsAway() == hop_start - hop_limit
    return p;
}

static meshtastic_Channel makeBehaviorChannel(meshtastic_Channel_Role role, const char *name)
{
    meshtastic_Channel channel = meshtastic_Channel_init_default;
    channel.has_settings = true;
    channel.role = role;
    channel.settings.has_module_settings = true;
    channel.settings.module_settings.position_precision = 16;
    strncpy(channel.settings.name, name, sizeof(channel.settings.name) - 1);
    return channel;
}

static void configureBehaviorChannels()
{
    memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = 2;

    meshtastic_Channel eventChannel = makeBehaviorChannel(meshtastic_Channel_Role_PRIMARY, "everyone");
    eventChannel.index = 0;
#ifdef USERPREFS_CHANNEL_0_PSK
    static const uint8_t eventPsk[] = USERPREFS_CHANNEL_0_PSK;
    eventChannel.settings.psk.size = sizeof(eventPsk);
    memcpy(eventChannel.settings.psk.bytes, eventPsk, sizeof(eventPsk));
#endif

    meshtastic_Channel privateChannel = makeBehaviorChannel(meshtastic_Channel_Role_SECONDARY, "private");
    privateChannel.index = 1;
    privateChannel.settings.psk.size = 32;
    memset(privateChannel.settings.psk.bytes, 0xAB, privateChannel.settings.psk.size);

    channelFile.channels[0] = eventChannel;
    channelFile.channels[1] = privateChannel;
    channels.onConfigChanged();
}

static meshtastic_MeshPacket makeBehaviorPacket(meshtastic_PortNum portnum, NodeNum from, NodeNum to, uint8_t channel,
                                                bool wantAck = false)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = nextBehaviorPacketId++;
    p.channel = channel;
    p.hop_start = 3;
    p.hop_limit = 3;
    p.relay_node = 0x22;
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    p.want_ack = wantAck;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = portnum;
    return p;
}

static meshtastic_MeshPacket *allocBehaviorPacket(meshtastic_PortNum portnum, NodeNum to, uint8_t channel, bool wantAck)
{
    auto packet = makeBehaviorPacket(portnum, kLocalNode, to, channel, wantAck);
    auto *allocated = packetPool.allocCopy(packet);
    TEST_ASSERT_NOT_NULL(allocated);
    return allocated;
}

void setUp(void)
{
    myNodeInfo.my_node_num = kLocalNode;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    config.lora.override_duty_cycle = true;
    config.security.private_key.size = 0;
    owner.is_licensed = false;
    mockNodeDB->clearTestNodes();
    shim->resetRouteHealthForTest();
    shim->clearPendingForTest();
    reliableShim->clearPendingForTest();
    if (nextHopRadio)
        nextHopRadio->reset();
    reliableRadio->reset();
    mockRoutingModule->ackNaks.clear();
    configureBehaviorChannels();
}

void tearDown(void) {}

// ===========================================================================
// Group 1 - resolveLastByte (M1)
// ===========================================================================

void test_resolve_none_when_empty(void)
{
    ResolvedNode r = mockNodeDB->resolveLastByte(0xAB, true);
    TEST_ASSERT_EQUAL(LastByteResolution::None, r.status);
}

void test_resolve_zero_byte_is_none(void)
{
    // 0 is the NO_RELAY_NODE / NO_NEXT_HOP_PREFERENCE sentinel - never resolves.
    mockNodeDB->addNode(0x22222200, 0, true, 60); // last byte maps to 0xFF, not 0
    TEST_ASSERT_EQUAL(LastByteResolution::None, mockNodeDB->resolveLastByte(0x00, true).status);
    TEST_ASSERT_EQUAL(LastByteResolution::None, mockNodeDB->resolveLastByte(0x00, false).status);
}

void test_resolve_unique_neighbor(void)
{
    mockNodeDB->addNode(0x000005AB, 0, true, 60); // direct, fresh, last byte 0xAB
    ResolvedNode r = mockNodeDB->resolveLastByte(0xAB, true);
    TEST_ASSERT_EQUAL(LastByteResolution::Unique, r.status);
    TEST_ASSERT_EQUAL_HEX32(0x000005AB, r.num);
}

void test_resolve_collision_is_ambiguous(void)
{
    // Birthday collision: two fresh direct neighbors share last byte 0xAB.
    mockNodeDB->addNode(0x000005AB, 0, true, 60);
    mockNodeDB->addNode(0x000006AB, 0, true, 60);
    ResolvedNode r = mockNodeDB->resolveLastByte(0xAB, true);
    TEST_ASSERT_EQUAL(LastByteResolution::Ambiguous, r.status);
    TEST_ASSERT_EQUAL_HEX32(0, r.num); // never silently picks one
}

void test_resolve_strict_excludes_stale(void)
{
    mockNodeDB->addNode(0x000005AB, 0, true, 60);                                // fresh
    mockNodeDB->addNode(0x000006AB, 0, true, NEXTHOP_NEIGHBOR_FRESH_SECS + 100); // stale
    ResolvedNode r = mockNodeDB->resolveLastByte(0xAB, true);
    TEST_ASSERT_EQUAL(LastByteResolution::Unique, r.status);
    TEST_ASSERT_EQUAL_HEX32(0x000005AB, r.num);
}

void test_resolve_strict_excludes_far(void)
{
    mockNodeDB->addNode(0x000005AB, 0, true, 60); // direct neighbor
    mockNodeDB->addNode(0x000006AB, 2, true, 60); // 2 hops away -> not a direct neighbor
    ResolvedNode r = mockNodeDB->resolveLastByte(0xAB, true);
    TEST_ASSERT_EQUAL(LastByteResolution::Unique, r.status);
    TEST_ASSERT_EQUAL_HEX32(0x000005AB, r.num);
}

void test_resolve_lenient_includes_favorite_router(void)
{
    // Unknown hop distance, but a favorite ROUTER: lenient gate accepts, strict gate does not.
    mockNodeDB->addNode(0x000007AB, 0, false, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/true);
    TEST_ASSERT_EQUAL(LastByteResolution::Unique, mockNodeDB->resolveLastByte(0xAB, false).status);
    TEST_ASSERT_EQUAL(LastByteResolution::None, mockNodeDB->resolveLastByte(0xAB, true).status);
}

void test_resolve_lenient_collision_favorite_plus_neighbor(void)
{
    mockNodeDB->addNode(0x000007AB, 0, false, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/true);
    mockNodeDB->addNode(0x000005AB, 0, true, 60); // direct neighbor, same byte
    TEST_ASSERT_EQUAL(LastByteResolution::Ambiguous, mockNodeDB->resolveLastByte(0xAB, false).status);
}

void test_resolve_skips_self(void)
{
    // A node equal to us (last byte 0x11) must never resolve to ourselves.
    mockNodeDB->addNode(kLocalNode, 0, true, 60);
    TEST_ASSERT_EQUAL(LastByteResolution::None, mockNodeDB->resolveLastByte(0x11, true).status);
}

void test_resolve_skips_ignored(void)
{
    mockNodeDB->addNode(0x000005AB, 0, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, /*ignored=*/true);
    TEST_ASSERT_EQUAL(LastByteResolution::None, mockNodeDB->resolveLastByte(0xAB, true).status);
}

void test_resolve_0x00_maps_to_0xFF_and_collides(void)
{
    // getLastByteOfNodeNum() maps ...00 -> 0xFF, so a ...00 node and a ...FF node collide on 0xFF.
    TEST_ASSERT_EQUAL_HEX8(0xFF, mockNodeDB->getLastByteOfNodeNum(0x11111100));
    mockNodeDB->addNode(0x11111100, 0, true, 60); // last byte 0xFF (remapped)
    TEST_ASSERT_EQUAL(LastByteResolution::Unique, mockNodeDB->resolveLastByte(0xFF, true).status);
    mockNodeDB->addNode(0x222222FF, 0, true, 60); // genuine ...FF
    TEST_ASSERT_EQUAL(LastByteResolution::Ambiguous, mockNodeDB->resolveLastByte(0xFF, true).status);
}

void test_resolve_unique_helper(void)
{
    mockNodeDB->addNode(0x000005AB, 0, true, 60);
    NodeNum out = 0;
    TEST_ASSERT_TRUE(mockNodeDB->resolveUniqueLastByte(0xAB, true, &out));
    TEST_ASSERT_EQUAL_HEX32(0x000005AB, out);
    mockNodeDB->addNode(0x000006AB, 0, true, 60); // create collision
    TEST_ASSERT_FALSE(mockNodeDB->resolveUniqueLastByte(0xAB, true));
}

// ===========================================================================
// Group 2 - getNextHop (M2 send-path gate + M3 decay)
// ===========================================================================

static constexpr NodeNum DEST = 0x000000B0; // DM destination (last byte 0xB0, distinct from 0xAB)

void test_getnexthop_unique_returns_byte(void)
{
    mockNodeDB->addNode(DEST, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, /*nextHop=*/0xAB);
    mockNodeDB->addNode(0x000005AB, 0, true, 60); // unique fresh neighbor with byte 0xAB
    auto nh = shim->getNextHop(DEST, /*relay=*/0x11);
    TEST_ASSERT_TRUE(nh.has_value());
    TEST_ASSERT_EQUAL_HEX8(0xAB, nh.value());
}

void test_getnexthop_ambiguous_floods(void)
{
    mockNodeDB->addNode(DEST, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, /*nextHop=*/0xAB);
    mockNodeDB->addNode(0x000005AB, 0, true, 60);
    mockNodeDB->addNode(0x000006AB, 0, true, 60); // collision -> ambiguous
    TEST_ASSERT_FALSE(shim->getNextHop(DEST, 0x11).has_value());
}

void test_getnexthop_vanished_neighbor_floods(void)
{
    mockNodeDB->addNode(DEST, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, /*nextHop=*/0xAB);
    // The only 0xAB node is stale -> strict gate yields None -> flood.
    mockNodeDB->addNode(0x000005AB, 0, true, NEXTHOP_NEIGHBOR_FRESH_SECS + 100);
    TEST_ASSERT_FALSE(shim->getNextHop(DEST, 0x11).has_value());
}

void test_getnexthop_split_horizon_floods(void)
{
    mockNodeDB->addNode(DEST, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, /*nextHop=*/0xAB);
    mockNodeDB->addNode(0x000005AB, 0, true, 60);
    // relay_node == stored next_hop -> don't send it back the way it came.
    TEST_ASSERT_FALSE(shim->getNextHop(DEST, /*relay=*/0xAB).has_value());
}

void test_getnexthop_broadcast_is_nullopt(void)
{
    TEST_ASSERT_FALSE(shim->getNextHop(NODENUM_BROADCAST, 0x11).has_value());
}

void test_getnexthop_decays_stale_route(void)
{
    mockNodeDB->addNode(DEST, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, /*nextHop=*/0xAB);
    mockNodeDB->addNode(0x000005AB, 0, true, 60); // a valid unique neighbor exists...
    // ...but the health record is older than the TTL, so the route should decay to flooding.
    shim->noteRouteLearned(DEST, 0xAB, millis() - (TTL + 5000));
    TEST_ASSERT_FALSE(shim->getNextHop(DEST, 0x11).has_value());
    // Decay also clears the persisted next_hop and the RAM health record.
    TEST_ASSERT_EQUAL_HEX8(NO_NEXT_HOP_PREFERENCE, mockNodeDB->getMeshNode(DEST)->next_hop);
    TEST_ASSERT_NULL(shim->findRouteHealth(DEST));
}

// ===========================================================================
// Group 3 - route-health helpers (M3)
// ===========================================================================

void test_health_fresh_not_stale(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    RouteHealth *h = shim->findRouteHealth(DEST);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_FALSE(shim->isRouteStale(*h, 1000 + TTL - 1));
}

void test_health_ttl_expiry(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    RouteHealth *h = shim->findRouteHealth(DEST);
    TEST_ASSERT_TRUE(shim->isRouteStale(*h, 1000 + TTL)); // boundary is inclusive (>=)
}

void test_health_ttl_rollover_safe(void)
{
    const uint32_t learnAt = 0xFFFFFFFFu - 1000; // learned just before the millis() rollover
    shim->noteRouteLearned(DEST, 0xAB, learnAt);
    RouteHealth *h = shim->findRouteHealth(DEST);
    // 1500 ms later (wrapped to now=500): unsigned subtraction yields ~1500 ms, not "stale".
    TEST_ASSERT_FALSE(shim->isRouteStale(*h, 500));
}

void test_health_failure_threshold(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    for (uint8_t i = 1; i < THRESH; i++)
        shim->noteRouteFailure(DEST);
    TEST_ASSERT_FALSE(shim->isRouteStale(*shim->findRouteHealth(DEST), 1000)); // THRESH-1 failures: ok
    shim->noteRouteFailure(DEST);
    TEST_ASSERT_TRUE(shim->isRouteStale(*shim->findRouteHealth(DEST), 1000)); // THRESH failures: stale
}

void test_health_success_resets_failures(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    shim->noteRouteFailure(DEST);
    shim->noteRouteFailure(DEST);
    shim->noteRouteSuccess(DEST, 2000);
    RouteHealth *h = shim->findRouteHealth(DEST);
    TEST_ASSERT_EQUAL_UINT8(0, h->consecutiveFailures);
    TEST_ASSERT_FALSE(shim->isRouteStale(*h, 2000));
}

void test_health_relearn_same_hop_keeps_failures(void)
{
    // Anti-flap: an asymmetric reverse path re-teaching the same dead hop must not reset failures.
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    shim->noteRouteFailure(DEST);
    shim->noteRouteFailure(DEST);
    shim->noteRouteLearned(DEST, 0xAB, 2000); // same hop
    TEST_ASSERT_EQUAL_UINT8(2, shim->findRouteHealth(DEST)->consecutiveFailures);
}

void test_health_relearn_new_hop_resets_failures(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    shim->noteRouteFailure(DEST);
    shim->noteRouteFailure(DEST);
    shim->noteRouteLearned(DEST, 0xCD, 2000); // genuinely new hop -> clean slate
    RouteHealth *h = shim->findRouteHealth(DEST);
    TEST_ASSERT_EQUAL_UINT8(0, h->consecutiveFailures);
    TEST_ASSERT_EQUAL_HEX8(0xCD, h->lastNextHop);
}

void test_health_failure_without_record_is_noop(void)
{
    shim->noteRouteFailure(DEST); // no record yet
    TEST_ASSERT_NULL(shim->findRouteHealth(DEST));
}

void test_health_clear(void)
{
    shim->noteRouteLearned(DEST, 0xAB, 1000);
    TEST_ASSERT_NOT_NULL(shim->findRouteHealth(DEST));
    shim->clearRouteHealth(DEST);
    TEST_ASSERT_NULL(shim->findRouteHealth(DEST));
}

void test_health_lru_eviction_bounds_table(void)
{
    // Fill every slot with increasing learn times, then add one more: the oldest must be evicted.
    for (uint8_t i = 0; i < HEALTH_MAX; i++)
        shim->noteRouteLearned(0x1000 + i, 0xAB, 1000 + (uint32_t)i * 1000);
    NodeNum oldest = 0x1000;
    TEST_ASSERT_NOT_NULL(shim->findRouteHealth(oldest));
    shim->noteRouteLearned(0x2000, 0xAB, 1000 + (uint32_t)HEALTH_MAX * 1000); // overflow
    TEST_ASSERT_NULL(shim->findRouteHealth(oldest));                          // evicted
    TEST_ASSERT_NOT_NULL(shim->findRouteHealth(0x2000));                      // newest present
}

// ===========================================================================
// Group 4 - shouldDecrementHopLimit favorite-router resolution (M2, site 4)
// ===========================================================================

void test_hoplimit_preserve_unique_favorite_router(void)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    mockNodeDB->addNode(0x000007AB, 0, true, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/true);
    meshtastic_MeshPacket p = makeRelayedPacket(/*relay=*/0xAB, /*hopsAway=*/1);
    TEST_ASSERT_FALSE(shim->shouldDecrementHopLimit(&p)); // preserve
}

void test_hoplimit_decrement_on_colliding_favorites(void)
{
    // Headline regression: two favorite routers share the relay byte -> ambiguous -> decrement
    // (the old "first NodeDB match wins" scan would non-deterministically preserve).
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    mockNodeDB->addNode(0x000007AB, 0, true, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/true);
    mockNodeDB->addNode(0x000008AB, 0, true, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/true);
    meshtastic_MeshPacket p = makeRelayedPacket(0xAB, 1);
    TEST_ASSERT_TRUE(shim->shouldDecrementHopLimit(&p)); // decrement
}

void test_hoplimit_decrement_when_resolved_not_favorite(void)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    mockNodeDB->addNode(0x000007AB, 0, true, 60, meshtastic_Config_DeviceConfig_Role_ROUTER, /*favorite=*/false);
    meshtastic_MeshPacket p = makeRelayedPacket(0xAB, 1);
    TEST_ASSERT_TRUE(shim->shouldDecrementHopLimit(&p)); // unique but not a favorite -> decrement
}

// ===========================================================================
// Group 5 - event-coordinate routing behavior
// ===========================================================================

void test_eventPolicy_reliableOriginSendSuppressesTxAndPending(void)
{
    ErrorCode result = reliableShim->send(
        allocBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, NODENUM_BROADCAST, /*event channel=*/0, /*wantAck=*/true));

    if (kEventPolicyEnabled) {
        TEST_ASSERT_EQUAL_INT(meshtastic_Routing_Error_NOT_AUTHORIZED, result);
        TEST_ASSERT_EQUAL_UINT32(0, reliableRadio->sentPackets.size());
        TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
    } else {
        TEST_ASSERT_EQUAL_INT(ERRNO_OK, result);
        TEST_ASSERT_EQUAL_UINT32(1, reliableRadio->sentPackets.size());
        TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
    }
}

void test_eventPolicy_reliablePrivateCoordinateStillSends(void)
{
    ErrorCode result = reliableShim->send(
        allocBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, NODENUM_BROADCAST, /*private channel=*/1, /*wantAck=*/true));

    TEST_ASSERT_EQUAL_INT(ERRNO_OK, result);
    TEST_ASSERT_EQUAL_UINT32(1, reliableRadio->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

void test_eventPolicy_floodingDuplicateSuppressesCoordinateButRelaysText(void)
{
    mockNodeDB->addNode(kRemoteNode, 0, true, 0);
    auto coordinate = makeBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, kRemoteNode, NODENUM_BROADCAST, 0);
    TEST_ASSERT_FALSE(shim->filterViaFlooding(&coordinate));
    TEST_ASSERT_TRUE(shim->filterViaFlooding(&coordinate));
    TEST_ASSERT_EQUAL_UINT32(kEventPolicyEnabled ? 0 : 1, nextHopRadio->sentPackets.size());

    nextHopRadio->reset();
    auto text = makeBehaviorPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, NODENUM_BROADCAST, 0);
    TEST_ASSERT_FALSE(shim->filterViaFlooding(&text));
    TEST_ASSERT_TRUE(shim->filterViaFlooding(&text));
    TEST_ASSERT_EQUAL_UINT32(1, nextHopRadio->sentPackets.size());
}

void test_eventPolicy_nextHopDuplicateSuppressesEventButRelaysPrivateCoordinate(void)
{
    mockNodeDB->addNode(kRemoteNode, 0, true, 0);
    auto eventCoordinate = makeBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, kRemoteNode, NODENUM_BROADCAST, 0);
    TEST_ASSERT_FALSE(shim->filterViaNextHop(&eventCoordinate));
    TEST_ASSERT_TRUE(shim->filterViaNextHop(&eventCoordinate));
    TEST_ASSERT_EQUAL_UINT32(kEventPolicyEnabled ? 0 : 1, nextHopRadio->sentPackets.size());

    nextHopRadio->reset();
    auto privateCoordinate = makeBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, kRemoteNode, NODENUM_BROADCAST, 1);
    TEST_ASSERT_FALSE(shim->filterViaNextHop(&privateCoordinate));
    TEST_ASSERT_TRUE(shim->filterViaNextHop(&privateCoordinate));
    TEST_ASSERT_EQUAL_UINT32(1, nextHopRadio->sentPackets.size());
}

void test_eventPolicy_repeatedLocalPacketSuppressesCoordinateAckButKeepsTextAck(void)
{
    mockNodeDB->addNode(kRemoteNode, 0, true, 0);
    auto coordinate = makeBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, kRemoteNode, kLocalNode, 0, /*wantAck=*/true);
    TEST_ASSERT_FALSE(shim->filterViaNextHop(&coordinate));
    TEST_ASSERT_TRUE(shim->filterViaNextHop(&coordinate));
    TEST_ASSERT_EQUAL_UINT32(kEventPolicyEnabled ? 0 : 1, mockRoutingModule->ackNaks.size());

    mockRoutingModule->ackNaks.clear();
    auto text = makeBehaviorPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kLocalNode, 0, /*wantAck=*/true);
    TEST_ASSERT_FALSE(shim->filterViaNextHop(&text));
    TEST_ASSERT_TRUE(shim->filterViaNextHop(&text));
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->ackNaks.size());
    const auto &ack = mockRoutingModule->ackNaks.front();
    TEST_ASSERT_EQUAL_INT(meshtastic_Routing_Error_NONE, std::get<0>(ack));
    TEST_ASSERT_EQUAL_HEX32(kRemoteNode, std::get<1>(ack));
    TEST_ASSERT_EQUAL_HEX32(text.id, std::get<2>(ack));
}

void test_eventPolicy_seededRetrySuppressesTxUntilGateOff(void)
{
    auto coordinate = makeBehaviorPacket(meshtastic_PortNum_WAYPOINT_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->seedRetry(coordinate, /*attempts=*/2);
    reliableShim->makeRetryDue(kLocalNode, coordinate.id);

    reliableShim->runDueRetries();

    TEST_ASSERT_EQUAL_UINT32(kEventPolicyEnabled ? 0 : 1, reliableRadio->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

void test_reliableAckStopsNormalPendingTransmission(void)
{
    auto original = makeBehaviorPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_RETX);
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());

    auto ack = makeBehaviorPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode, kLocalNode, 1);
    ack.decoded.request_id = original.id;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

// A PKI DM we originated is encrypted to the recipient, so when we overhear it being rebroadcast we
// cannot decode it. The routing auth gate classifies it opaque and returns before
// shouldFilterReceived() runs, so the implicit ACK has to be reachable from the header alone -
// otherwise the client never sees "Delivered to mesh" for a DM.
void test_implicit_ack_for_opaque_own_packet(void)
{
    auto original = makeBehaviorPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 0, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
    mockRoutingModule->ackNaks.clear();

    // The overheard copy as it actually arrives: still encrypted, nothing decoded.
    meshtastic_MeshPacket overheard = meshtastic_MeshPacket_init_zero;
    overheard.from = kLocalNode;
    overheard.to = kRemoteNode;
    overheard.id = original.id;
    overheard.channel = 0;
    overheard.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    overheard.encrypted.size = 32;

    reliableShim->implicitAckForTest(&overheard);

    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->ackNaks.size());
    const auto &ack = mockRoutingModule->ackNaks.front();
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, std::get<0>(ack));
    TEST_ASSERT_EQUAL_UINT32(kLocalNode, std::get<1>(ack)); // addressed to us -> reaches the phone
    TEST_ASSERT_EQUAL_UINT32(original.id, std::get<2>(ack));

    reliableShim->clearPendingForTest();
}

// Someone else's traffic must never mint an ACK, even with a colliding id.
void test_implicit_ack_ignores_foreign_pkt(void)
{
    auto original = makeBehaviorPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 0, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    mockRoutingModule->ackNaks.clear();

    meshtastic_MeshPacket foreign = meshtastic_MeshPacket_init_zero;
    foreign.from = kRemoteNode;
    foreign.to = kLocalNode;
    foreign.id = original.id;
    foreign.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    foreign.encrypted.size = 32;

    reliableShim->implicitAckForTest(&foreign);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
    reliableShim->clearPendingForTest();
}

void test_pending_does_not_cancel_radio_queue_before_first_retry(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(0x33333333);
    p.from = kLocalNode;
    p.id = 0x51000001;
    shim->trackForTest(p, 5);

    TEST_ASSERT_TRUE(shim->stopForTest(kLocalNode, p.id));
    TEST_ASSERT_EQUAL_UINT32(0, mockIface->cancelCount);
}

void test_pending_cancels_radio_queue_after_first_retry_for_any_budget(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(0x33333333);
    p.from = kLocalNode;
    p.id = 0x51000002;
    shim->trackForTest(p, 5);
    shim->markOneRetryFiredForTest(kLocalNode, p.id);

    TEST_ASSERT_TRUE(shim->stopForTest(kLocalNode, p.id));
    TEST_ASSERT_EQUAL_UINT32(1, mockIface->cancelCount);
}

void test_directed_hop_tracks_three_total_attempts(void)
{
    installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(0x33333333);
    p.id = 0x51530003;

    PendingPacket *entry = shim->trackWithDefaultBudgetForTest(p);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT8(3, entry->initialNumRetransmissions + 1);
    TEST_ASSERT_TRUE(shim->stopForTest(p.from, p.id));
}

void test_intermediate_three_attempts_preserve_record_and_flood_last(void)
{
    MockRadioInterface *mockIface = installMockIface();
    constexpr NodeNum dest = 0x33333333;
    mockNodeDB->addNode(dest, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, 0xAB);
    mockNodeDB->addNode(0x000007AB, 0, true, 60);

    meshtastic_MeshPacket p = makeRebroadcastCandidate(dest);
    p.id = 0x51530004;
    p.next_hop = 0xAB;
    PendingPacket *entry = shim->trackWithDefaultBudgetForTest(p);
    TEST_ASSERT_NOT_NULL(entry);
    meshtastic_MeshPacket *trackedPacket = entry->packet;

    shim->fireNextRetryForTest(p.from, p.id);
    TEST_ASSERT_EQUAL_UINT32(1, mockIface->sentNextHops.size());
#if NEXTHOP_EARLY_FLOOD_ON_UNVERIFIED
    TEST_ASSERT_EQUAL_HEX8(NO_NEXT_HOP_PREFERENCE, mockIface->sentNextHops[0]);
#else
    TEST_ASSERT_EQUAL_HEX8(0xAB, mockIface->sentNextHops[0]);
#endif
    TEST_ASSERT_EQUAL_PTR(trackedPacket, shim->pendingPacketForTest(p.from, p.id));

    shim->fireNextRetryForTest(p.from, p.id);
    TEST_ASSERT_EQUAL_UINT32(2, mockIface->sentNextHops.size());
    TEST_ASSERT_EQUAL_HEX8(NO_NEXT_HOP_PREFERENCE, mockIface->sentNextHops[1]);
    TEST_ASSERT_TRUE(shim->stopForTest(p.from, p.id));
}

void test_early_flood_preserves_fresh_verified_route(void)
{
    MockRadioInterface *mockIface = installMockIface();
    constexpr NodeNum dest = 0x33333333;
    mockNodeDB->addNode(dest, 2, true, 60, meshtastic_Config_DeviceConfig_Role_CLIENT, false, false, 0xAB);
    mockNodeDB->addNode(0x000007AB, 0, true, 60);
    shim->noteRouteLearned(dest, 0xAB, millis());

    meshtastic_MeshPacket p = makeRebroadcastCandidate(dest);
    p.id = 0x51530005;
    p.next_hop = 0xAB;
    TEST_ASSERT_NOT_NULL(shim->trackWithDefaultBudgetForTest(p));

    shim->fireNextRetryForTest(p.from, p.id);
    TEST_ASSERT_EQUAL_UINT32(1, mockIface->sentNextHops.size());
    TEST_ASSERT_EQUAL_HEX8(0xAB, mockIface->sentNextHops[0]);
    TEST_ASSERT_TRUE(shim->stopForTest(p.from, p.id));
}

// Control: proves the NO_LORA case below turns on the `to` field alone.
void test_rebroadcast_normal_broadcast_is_relayed(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(NODENUM_BROADCAST);

    TEST_ASSERT_TRUE_MESSAGE(shim->perhapsRebroadcast(&p), "ordinary broadcast must be rebroadcast");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockIface->sendCount, "exactly one packet should reach the radio");
}

void test_rebroadcast_no_lora_broadcast_is_not_relayed(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(NODENUM_BROADCAST_NO_LORA);

    TEST_ASSERT_FALSE_MESSAGE(shim->perhapsRebroadcast(&p), "no-LoRa broadcast must not be rebroadcast");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockIface->sendCount, "no packet should be handed to the radio at all");
}

void test_rebroadcast_declined_send_releases_packet(void)
{
    MockRadioInterface *mockIface = installMockIface();
    mockIface->declineAll = true;
    meshtastic_MeshPacket p = makeRebroadcastCandidate(NODENUM_BROADCAST);

    TEST_ASSERT_TRUE_MESSAGE(shim->perhapsRebroadcast(&p), "the rebroadcast must still be attempted");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockIface->sendCount, "the copy must have reached the mock radio");
}

// An already-encrypted packet never reaches perhapsEncode's TOO_LARGE check, so Router::send() is the
// last gate before the radio queue: MeshPacket.encrypted holds 256 bytes, the radio buffer 240.
void test_send_rejects_payload_larger_than_radio_buffer(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(NODENUM_BROADCAST);
    p.id = 0x51000010;
    p.encrypted.size = MAX_RADIO_PAYLOAD_LEN + 1;

    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_TOO_LARGE, shim->send(packetPool.allocCopy(p)),
                              "a payload larger than the radio buffer must be refused");
    TEST_ASSERT_EQUAL_MESSAGE(0, mockIface->sendCount, "the oversized packet must never reach the radio");

    p.id = 0x51000011;
    p.encrypted.size = MAX_RADIO_PAYLOAD_LEN;
    TEST_ASSERT_EQUAL_MESSAGE(ERRNO_OK, shim->send(packetPool.allocCopy(p)), "a payload at the radio ceiling must still be sent");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockIface->sendCount, "the fitting packet must reach the radio");
}

#if USERPREFS_EVENT_MODE
void test_event_mode_hop_behavior(void)
{
    MockRadioInterface *mockIface = installMockIface();
    meshtastic_MeshPacket p = makeRebroadcastCandidate(NODENUM_BROADCAST);
    p.from = kLocalNode;
    p.hop_start = 0;
    p.hop_limit = HOP_MAX;

    TEST_ASSERT_EQUAL(ERRNO_OK, shim->send(packetPool.allocCopy(p)));
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, mockIface->lastHopLimit);
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, mockIface->lastHopStart);

    p = makeRebroadcastCandidate(NODENUM_BROADCAST);
    p.hop_start = HOP_MAX;
    p.hop_limit = HOP_MAX;

    TEST_ASSERT_TRUE(shim->perhapsRebroadcast(&p));
    TEST_ASSERT_EQUAL_UINT8(Default::eventModeRelayHopLimit, mockIface->lastHopLimit);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Default::eventModeRelayHopLimit + 1), mockIface->lastHopStart);

    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    p = makeRebroadcastCandidate(NODENUM_BROADCAST);
    p.hop_start = HOP_MAX;
    p.hop_limit = HOP_MAX;

    TEST_ASSERT_TRUE(shim->relayOpaquePacket(&p));
    TEST_ASSERT_EQUAL_UINT8(Default::eventModeRelayHopLimit, mockIface->lastHopLimit);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Default::eventModeRelayHopLimit + 1), mockIface->lastHopStart);

    p = makeRebroadcastCandidate(NODENUM_BROADCAST);
    p.hop_start = 0;
    p.hop_limit = HOP_MAX;

    TEST_ASSERT_TRUE(shim->relayOpaquePacket(&p));
    TEST_ASSERT_EQUAL_UINT8(Default::eventModeRelayHopLimit, mockIface->lastHopLimit);
    TEST_ASSERT_EQUAL_UINT8(0, mockIface->lastHopStart);
}
#endif

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    AirTime testAirTime;
    airTime = &testAirTime;
    UNITY_BEGIN();

    airTimeFixture = std::make_unique<ScopedAirTimeFixture>();
    mockNodeDB = new MockNodeDB();
    shim = new NextHopRouterTestShim();
    reliableShim = new ReliableRouterTestShim();
    nodeDB = mockNodeDB;

    auto nextRadio = std::make_unique<CaptureRadioInterface>();
    nextHopRadio = nextRadio.get();
    shim->addInterface(std::move(nextRadio));

    auto reliableCapture = std::make_unique<CaptureRadioInterface>();
    reliableRadio = reliableCapture.get();
    reliableShim->addInterface(std::move(reliableCapture));

    mockRoutingModule = new MockRoutingModule();
    routingModule = mockRoutingModule;

    printf("\n=== resolveLastByte (M1) ===\n");
    RUN_TEST(test_resolve_none_when_empty);
    RUN_TEST(test_resolve_zero_byte_is_none);
    RUN_TEST(test_resolve_unique_neighbor);
    RUN_TEST(test_resolve_collision_is_ambiguous);
    RUN_TEST(test_resolve_strict_excludes_stale);
    RUN_TEST(test_resolve_strict_excludes_far);
    RUN_TEST(test_resolve_lenient_includes_favorite_router);
    RUN_TEST(test_resolve_lenient_collision_favorite_plus_neighbor);
    RUN_TEST(test_resolve_skips_self);
    RUN_TEST(test_resolve_skips_ignored);
    RUN_TEST(test_resolve_0x00_maps_to_0xFF_and_collides);
    RUN_TEST(test_resolve_unique_helper);

    printf("\n=== getNextHop (M2 + M3 decay) ===\n");
    RUN_TEST(test_getnexthop_unique_returns_byte);
    RUN_TEST(test_getnexthop_ambiguous_floods);
    RUN_TEST(test_getnexthop_vanished_neighbor_floods);
    RUN_TEST(test_getnexthop_split_horizon_floods);
    RUN_TEST(test_getnexthop_broadcast_is_nullopt);
    RUN_TEST(test_getnexthop_decays_stale_route);

    printf("\n=== route-health helpers (M3) ===\n");
    RUN_TEST(test_health_fresh_not_stale);
    RUN_TEST(test_health_ttl_expiry);
    RUN_TEST(test_health_ttl_rollover_safe);
    RUN_TEST(test_health_failure_threshold);
    RUN_TEST(test_health_success_resets_failures);
    RUN_TEST(test_health_relearn_same_hop_keeps_failures);
    RUN_TEST(test_health_relearn_new_hop_resets_failures);
    RUN_TEST(test_health_failure_without_record_is_noop);
    RUN_TEST(test_health_clear);
    RUN_TEST(test_health_lru_eviction_bounds_table);

    printf("\n=== shouldDecrementHopLimit (M2 site 4) ===\n");
    RUN_TEST(test_hoplimit_preserve_unique_favorite_router);
    RUN_TEST(test_hoplimit_decrement_on_colliding_favorites);
    RUN_TEST(test_hoplimit_decrement_when_resolved_not_favorite);

    printf("\n=== event-coordinate routing behavior ===\n");
    RUN_TEST(test_eventPolicy_reliableOriginSendSuppressesTxAndPending);
    RUN_TEST(test_eventPolicy_reliablePrivateCoordinateStillSends);
    RUN_TEST(test_eventPolicy_floodingDuplicateSuppressesCoordinateButRelaysText);
    RUN_TEST(test_eventPolicy_nextHopDuplicateSuppressesEventButRelaysPrivateCoordinate);
    RUN_TEST(test_eventPolicy_repeatedLocalPacketSuppressesCoordinateAckButKeepsTextAck);
    RUN_TEST(test_eventPolicy_seededRetrySuppressesTxUntilGateOff);
    RUN_TEST(test_reliableAckStopsNormalPendingTransmission);

    printf("\n=== pending retransmission bookkeeping ===\n");
    RUN_TEST(test_implicit_ack_for_opaque_own_packet);
    RUN_TEST(test_implicit_ack_ignores_foreign_pkt);
    RUN_TEST(test_pending_does_not_cancel_radio_queue_before_first_retry);
    RUN_TEST(test_pending_cancels_radio_queue_after_first_retry_for_any_budget);
    RUN_TEST(test_directed_hop_tracks_three_total_attempts);
    RUN_TEST(test_intermediate_three_attempts_preserve_record_and_flood_last);
    RUN_TEST(test_early_flood_preserves_fresh_verified_route);

    printf("\n=== rebroadcast of NODENUM_BROADCAST_NO_LORA ===\n");
    RUN_TEST(test_rebroadcast_normal_broadcast_is_relayed);
    RUN_TEST(test_rebroadcast_no_lora_broadcast_is_not_relayed);
    RUN_TEST(test_rebroadcast_declined_send_releases_packet);
    RUN_TEST(test_send_rejects_payload_larger_than_radio_buffer);
#if USERPREFS_EVENT_MODE
    RUN_TEST(test_event_mode_hop_behavior);
#endif

    int result = UNITY_END();
    airTimeFixture.reset();
    exit(result);
}

void loop() {}
