// Unit tests for NextHop direct-message reliability mitigations (see docs/nexthop-routing-reliability.md):
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
#include "mesh/NextHopRouter.h"
#include "mesh/NodeDB.h"
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
    using Router::shouldDecrementHopLimit; // protected in Router

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

static MockNodeDB *mockNodeDB = nullptr;
static NextHopRouterTestShim *shim = nullptr;
static ReliableRouterTestShim *reliableShim = nullptr;
static CaptureRadioInterface *nextHopRadio = nullptr;
static CaptureRadioInterface *reliableRadio = nullptr;
static MockRoutingModule *mockRoutingModule = nullptr;
static std::unique_ptr<ScopedAirTimeFixture> airTimeFixture;
static PacketId nextBehaviorPacketId = 0x70000000;

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
    strcpy(channel.settings.name, name);
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

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
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

    int result = UNITY_END();
    airTimeFixture.reset();
    exit(result);
}

void loop() {}
