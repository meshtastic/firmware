// TX-lifecycle publication for reliable sends we originate: NextHopRouter::resolveOwnTx() /
// notifyTxAck() / countOutstandingOwnTx() (src/mesh/NextHopRouter.cpp) and the four sites in
// ReliableRouter (src/mesh/ReliableRouter.cpp) that drive them - send(), sniffReceived()'s ACK and
// NAK arms, perhapsGenerateImplicitAckForOwnOverheard(), and NextHopRouter::doRetransmissions()'s
// retry-exhaustion arm.
//
// Why this behavior is required. `txAckStatusObservable` is what a status consumer (today
// StatusLEDModule's LED_TX_ACK indication; tomorrow a display, buzzer or telemetry sink) uses to
// decide whether this node is still waiting for confirmation of something it sent. The contract it
// relies on is narrow and easy to break:
//
//   * Exactly one terminal event per reliable send we originate. Both the implicit-ACK site and
//     sniffReceived() can reach the same pending record - the implicit ACK is minted as a local
//     routing packet whose loopback lands in sniffReceived() - so both are funnelled through
//     resolveOwnTx(), which publishes only when its own stopRetransmission() was the call that
//     removed the record.
//   * Only our own traffic. `pending` also holds records for packets we merely relay
//     (NextHopRouter::sendWithNextHop), and those must never move the indicator.
//   * `outstanding` is authoritative and is read after the transition, counting our unresolved
//     reliable packets. A consumer therefore needs no bookkeeping, and multiple simultaneous
//     reliable sends work without one.
//   * Meshtastic's own acknowledgement semantics, unchanged: an overheard LoRa rebroadcast of our
//     own packet is the implicit ACK (evidence the packet entered the mesh - NOT that every node
//     received a broadcast), a routing packet with error_reason NONE is the explicit ACK, and a
//     routing packet with an error, or exhausted retries, is the failure.
//
// The regression guarded. Delete or relax these assertions and the indicator silently latches or
// lies: a double terminal event on the implicit-ACK path (indicator flashes failure after a
// success, or a success event fires with the record still pending), a leaked record after the
// ACK/NAK for a *different* packet or after an unrelated inbound frame (indicator stuck on
// forever, which on a battery tracker is a power bug), an event for a relayed packet (indicator
// reacts to mesh traffic this node never sent), a terminal event on a mere retransmission
// (indicator clears while delivery is still unconfirmed), or an `outstanding` that ignores the
// other packets still in flight - which is exactly the bug a single `bool waitingForAck` would
// have.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/ReliableRouter.h"
#include "mesh/TxAckStatus.h"
#include "modules/RoutingModule.h"
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <tuple>
#include <vector>

static constexpr NodeNum kLocalNode = 0x11111111; // last byte 0x11
static constexpr NodeNum kRemoteNode = 0x22222222;
static constexpr NodeNum kThirdNode = 0x33333333;

// ---------------------------------------------------------------------------
// MockNodeDB - the routers look sender records up on every sniff; an empty DB is enough here.
// Harness shape copied from test_reliable_ack_matrix / test_nexthop_routing.
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

    void addNode(NodeNum num)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.last_heard = getTime();
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

class ReliableRouterTestShim : public ReliableRouter
{
  public:
    ReliableRouterTestShim() : ReliableRouter() {}

    size_t pendingCount() const { return pending.size(); }
    uint8_t outstandingOwn() { return countOutstandingOwnTx(); }
    bool hasPending(NodeNum from, PacketId id) { return findPendingPacket(from, id) != nullptr; }

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

    bool filterForTest(const meshtastic_MeshPacket *p) { return ReliableRouter::shouldFilterReceived(p); }

    void implicitAckForTest(const meshtastic_MeshPacket *p) { perhapsGenerateImplicitAckForOwnOverheard(p); }

    // Clears state without publishing: setUp() must not leave events in the capture.
    void clearPendingForTest()
    {
        while (!pending.empty())
            stopRetransmission(pending.begin()->first);
    }
};

class CaptureRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sentPackets.push_back(*p);
        packetPool.release(p);
        return sendResult;
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
        sendResult = ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
    uint32_t cancelCount = 0;
    ErrorCode sendResult = ERRNO_OK;
};

// Records the ack/nak calls without performing the local loopback, so each publication site can be
// driven in isolation.
class MockRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false, const meshtastic_MeshPacket *relaySource = nullptr) override
    {
        (void)relaySource;
        ackNaks.emplace_back(err, to, idFrom, chIndex, hopLimit, ackWantsAck);
    }

    std::list<std::tuple<meshtastic_Routing_Error, NodeNum, PacketId, ChannelIndex, uint8_t, bool>> ackNaks;
};

// ---------------------------------------------------------------------------
// The observable under test, captured exactly as a real consumer would see it.
// ---------------------------------------------------------------------------
class TxAckCapture : public Observer<const TxAckEvent *>
{
  public:
    std::vector<TxAckEvent> events;

    void clear() { events.clear(); }
    size_t count() const { return events.size(); }
    const TxAckEvent &last() const
    {
        TEST_ASSERT_FALSE_MESSAGE(events.empty(), "expected a TX-lifecycle event");
        return events.back();
    }

  protected:
    int onNotify(const TxAckEvent *event) override
    {
        events.push_back(*event); // the router publishes a stack temporary
        return 0;
    }
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
static ReliableRouterTestShim *reliableShim = nullptr;
static CaptureRadio *radio = nullptr;
static MockRoutingModule *mockRoutingModule = nullptr;
static TxAckCapture *capture = nullptr;
static std::unique_ptr<ScopedAirTimeFixture> airTimeFixture;
static PacketId nextTestPacketId = 0x6B000000;

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

static meshtastic_MeshPacket makePacket(meshtastic_PortNum portnum, NodeNum from, NodeNum to, uint8_t channel, bool wantAck)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = nextTestPacketId++;
    p.channel = channel;
    p.hop_start = 3;
    p.hop_limit = 3; // hop_start == hop_limit -> getHopsAway() == 0 ("heard directly")
    p.relay_node = 0x22;
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    p.want_ack = wantAck;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = portnum;
    return p;
}

// A routing packet addressed to us carrying request_id - the shape of both an explicit ACK
// (error NONE) and a NAK (any other error).
static meshtastic_MeshPacket makeRoutingResponse(PacketId requestId, NodeNum from)
{
    auto p = makePacket(meshtastic_PortNum_ROUTING_APP, from, kLocalNode, 1, /*wantAck=*/false);
    p.decoded.request_id = requestId;
    p.priority = meshtastic_MeshPacket_Priority_ACK;
    return p;
}

static meshtastic_Routing makeRouting(meshtastic_Routing_Error err)
{
    meshtastic_Routing r = meshtastic_Routing_init_default;
    r.which_variant = meshtastic_Routing_error_reason_tag;
    r.error_reason = err;
    return r;
}

// Send a reliable packet the way MeshService would, and hand back the id/destination it used.
static std::tuple<PacketId, NodeNum, ErrorCode> sendReliable(NodeNum to, uint8_t channel)
{
    auto p = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, to, channel, /*wantAck=*/true);
    auto *allocated = packetPool.allocCopy(p);
    TEST_ASSERT_NOT_NULL(allocated);
    ErrorCode result = reliableShim->send(allocated);
    return std::make_tuple(p.id, to, result);
}

static void expectEvent(size_t index, PacketId id, NodeNum to, TxAckState state, meshtastic_Routing_Error err,
                        uint8_t outstanding)
{
    TEST_ASSERT_GREATER_THAN_UINT32(index, capture->count());
    const TxAckEvent &e = capture->events[index];
    TEST_ASSERT_EQUAL_HEX32(id, e.id);
    TEST_ASSERT_EQUAL_HEX32(to, e.to);
    TEST_ASSERT_EQUAL_INT((int)state, (int)e.state);
    TEST_ASSERT_EQUAL_INT(err, e.error);
    TEST_ASSERT_EQUAL_UINT8(outstanding, e.outstanding);
}

static void configureChannels()
{
    memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = 2;

    meshtastic_Channel primary = meshtastic_Channel_init_default;
    primary.index = 0;
    primary.has_settings = true;
    primary.role = meshtastic_Channel_Role_PRIMARY;
    strncpy(primary.settings.name, "primary", sizeof(primary.settings.name) - 1);

    meshtastic_Channel secondary = meshtastic_Channel_init_default;
    secondary.index = 1;
    secondary.has_settings = true;
    secondary.role = meshtastic_Channel_Role_SECONDARY;
    strncpy(secondary.settings.name, "second", sizeof(secondary.settings.name) - 1);
    secondary.settings.psk.size = 32;
    memset(secondary.settings.psk.bytes, 0xAB, secondary.settings.psk.size);

    channelFile.channels[0] = primary;
    channelFile.channels[1] = secondary;
    channels.onConfigChanged();
}

void setUp(void)
{
    myNodeInfo.my_node_num = kLocalNode;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    config.lora.override_duty_cycle = true;
    config.lora.hop_limit = 3;
    config.security.private_key.size = 0;
    owner.is_licensed = false;
    owner.public_key.size = 0;
    mockNodeDB->clearTestNodes();
    mockNodeDB->addNode(kRemoteNode);
    reliableShim->clearPendingForTest();
    radio->reset();
    mockRoutingModule->ackNaks.clear();
    configureChannels();
    capture->clear(); // last: clearing pending above must not leave events behind
}

void tearDown(void) {}

// ===========================================================================
// Group 1 - a reliable send of ours enters PENDING; nothing else does
// ===========================================================================

void test_reliable_dm_send_publishes_pending(void)
{
    PacketId id;
    NodeNum to;
    ErrorCode result;
    std::tie(id, to, result) = sendReliable(kRemoteNode, 1);

    TEST_ASSERT_EQUAL_INT(ERRNO_OK, result);
    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, id, to, TxAckState::PENDING, meshtastic_Routing_Error_NONE, 1);
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, id));
}

void test_reliable_broadcast_send_publishes_pending(void)
{
    // A want_ack broadcast is tracked too: its confirmation is an overheard rebroadcast, and the
    // event's `to` is what tells a consumer which acknowledgement semantics apply.
    PacketId id;
    NodeNum to;
    ErrorCode result;
    std::tie(id, to, result) = sendReliable(NODENUM_BROADCAST, 0);

    TEST_ASSERT_EQUAL_INT(ERRNO_OK, result);
    expectEvent(0, id, NODENUM_BROADCAST, TxAckState::PENDING, meshtastic_Routing_Error_NONE, 1);
    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
}

void test_unreliable_send_publishes_nothing(void)
{
    // No want_ack means there is no acknowledgement to wait for, so there is no lifecycle to report.
    auto p = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/false);
    TEST_ASSERT_EQUAL_INT(ERRNO_OK, reliableShim->send(packetPool.allocCopy(p)));

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_relayed_want_ack_send_publishes_nothing(void)
{
    // Someone else's want_ack packet passing through us is tracked in `pending` but is not ours;
    // the indicator must not react to mesh traffic this node never originated.
    auto p = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kThirdNode, 1, /*wantAck=*/true);
    reliableShim->send(packetPool.allocCopy(p));

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE_MESSAGE(reliableShim->pendingCount() > 0, "the relayed packet is still retransmission-tracked");
    TEST_ASSERT_EQUAL_UINT8(0, reliableShim->outstandingOwn());
}

// ===========================================================================
// Group 2 - acknowledgement, implicit and explicit
// ===========================================================================

void test_implicit_ack_publishes_acknowledged(void)
{
    // Someone rebroadcast our own packet over LoRa: evidence it entered the mesh.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_RETX);
    capture->clear();

    auto overheard = ours; // the same (from,id), heard back from a relayer
    overheard.relay_node = 0x44;
    reliableShim->implicitAckForTest(&overheard);

    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, ours.id, NODENUM_BROADCAST, TxAckState::ACKNOWLEDGED, meshtastic_Routing_Error_NONE, 0);
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, ours.id));
}

void test_implicit_ack_via_mqtt_keeps_pending(void)
{
    // An MQTT echo is not evidence the packet entered the LoRa mesh: retransmissions continue, so
    // the indicator must stay on and nothing terminal may be published.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_RETX);
    capture->clear();

    auto overheard = ours;
    overheard.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    reliableShim->implicitAckForTest(&overheard);

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, ours.id));
}

void test_explicit_routing_ack_publishes_acknowledged(void)
{
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    capture->clear();

    auto ack = makeRoutingResponse(ours.id, kRemoteNode);
    auto routing = makeRouting(meshtastic_Routing_Error_NONE);
    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, ours.id, kRemoteNode, TxAckState::ACKNOWLEDGED, meshtastic_Routing_Error_NONE, 0);
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, ours.id));
}

void test_explicit_nak_publishes_failed_with_reason(void)
{
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    capture->clear();

    auto nak = makeRoutingResponse(ours.id, kRemoteNode);
    auto routing = makeRouting(meshtastic_Routing_Error_NO_CHANNEL);
    reliableShim->sniffForTest(&nak, &routing);

    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, ours.id, kRemoteNode, TxAckState::FAILED, meshtastic_Routing_Error_NO_CHANNEL, 0);
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, ours.id));
}

// ===========================================================================
// Group 3 - nothing else may disturb an outstanding send
// ===========================================================================

void test_retransmission_publishes_nothing_and_keeps_pending(void)
{
    // A retry is not a verdict. The record survives with one attempt spent, and the indicator must
    // not be reset or cleared by it.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_RETX);
    capture->clear();

    reliableShim->makeRetryDue(kLocalNode, ours.id);
    reliableShim->runDueRetries();

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, ours.id));
    TEST_ASSERT_EQUAL_UINT8(1, reliableShim->outstandingOwn());
    TEST_ASSERT_EQUAL_MESSAGE(1, radio->sentPackets.size(), "the retry must actually have gone out");
}

void test_unrelated_inbound_packet_keeps_pending(void)
{
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    capture->clear();

    auto inbound = makePacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/false);
    reliableShim->filterForTest(&inbound);
    reliableShim->sniffForTest(&inbound, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, ours.id));
}

void test_ack_for_another_packet_keeps_pending(void)
{
    // A well-formed ACK whose request_id is not ours must leave the record - and the indicator -
    // exactly as they were.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    capture->clear();

    auto ack = makeRoutingResponse(ours.id ^ 0x5A5A, kRemoteNode);
    auto routing = makeRouting(meshtastic_Routing_Error_NONE);
    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, ours.id));
    TEST_ASSERT_EQUAL_UINT8(1, reliableShim->outstandingOwn());
}

void test_overheard_foreign_packet_keeps_pending(void)
{
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_RETX);
    capture->clear();

    auto foreign = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    reliableShim->implicitAckForTest(&foreign);

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, ours.id));
}

// ===========================================================================
// Group 4 - failure
// ===========================================================================

void test_retry_exhaustion_publishes_failed(void)
{
    // One attempt total leaves numRetransmissions == 0, so the next due pass is the exhaustion arm.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, 1);
    capture->clear();

    reliableShim->makeRetryDue(kLocalNode, ours.id);
    reliableShim->runDueRetries();

    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, ours.id, kRemoteNode, TxAckState::FAILED, meshtastic_Routing_Error_MAX_RETRANSMIT, 0);
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, ours.id));
    TEST_ASSERT_EQUAL_MESSAGE(1, mockRoutingModule->ackNaks.size(), "exhaustion still naks the originator");
    TEST_ASSERT_EQUAL_INT(meshtastic_Routing_Error_MAX_RETRANSMIT, std::get<0>(mockRoutingModule->ackNaks.front()));
}

void test_exhaustion_of_relayed_packet_publishes_nothing(void)
{
    auto foreign = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kThirdNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(foreign, 1);
    capture->clear();

    reliableShim->makeRetryDue(kRemoteNode, foreign.id);
    reliableShim->runDueRetries();

    TEST_ASSERT_EQUAL_UINT32(0, capture->count());
    TEST_ASSERT_FALSE(reliableShim->hasPending(kRemoteNode, foreign.id));
}

void test_iface_refusal_reports_failed_without_a_bogus_reason(void)
{
    // The radio refusing the packet yields ERRNO_UNKNOWN (32), which is also
    // meshtastic_Routing_Error_BAD_REQUEST. Publishing the raw code would invent a routing reason
    // the mesh never gave, so the event must carry NONE and let FAILED speak for itself.
    radio->sendResult = ERRNO_UNKNOWN;

    PacketId id;
    NodeNum to;
    ErrorCode result;
    std::tie(id, to, result) = sendReliable(kRemoteNode, 1);

    TEST_ASSERT_EQUAL_INT(ERRNO_UNKNOWN, result);
    expectEvent(0, id, kRemoteNode, TxAckState::FAILED, meshtastic_Routing_Error_NONE, 0);
    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, id));
}

void test_rejected_send_publishes_failed(void)
{
    // Router::send() refuses a payload larger than the radio buffer after ReliableRouter::send()
    // has already started tracking it, so the record must resolve as FAILED rather than leak.
    auto p = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = MAX_RADIO_PAYLOAD_LEN + 1;

    ErrorCode result = reliableShim->send(packetPool.allocCopy(p));

    TEST_ASSERT_EQUAL_INT(meshtastic_Routing_Error_TOO_LARGE, result);
    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    // TOO_LARGE is below the range where ERRNO_* and meshtastic_Routing_Error collide, so
    // sendFailureReason() must pass it through rather than flatten it to NONE.
    expectEvent(0, p.id, kRemoteNode, TxAckState::FAILED, meshtastic_Routing_Error_TOO_LARGE, 0);
    TEST_ASSERT_FALSE(reliableShim->hasPending(kLocalNode, p.id));
}

// ===========================================================================
// Group 5 - several reliable sends in flight at once
// ===========================================================================

void test_two_outstanding_sends_resolve_independently(void)
{
    // The case a single `bool waitingForAck` gets wrong: resolving one send while another is still
    // unconfirmed must report outstanding == 1, so a consumer keeps indicating.
    PacketId dmId, bcId;
    NodeNum ignoredTo;
    ErrorCode ignoredResult;
    std::tie(dmId, ignoredTo, ignoredResult) = sendReliable(kRemoteNode, 1);
    std::tie(bcId, ignoredTo, ignoredResult) = sendReliable(NODENUM_BROADCAST, 0);

    TEST_ASSERT_EQUAL_UINT32(2, capture->count());
    expectEvent(0, dmId, kRemoteNode, TxAckState::PENDING, meshtastic_Routing_Error_NONE, 1);
    expectEvent(1, bcId, NODENUM_BROADCAST, TxAckState::PENDING, meshtastic_Routing_Error_NONE, 2);

    // Explicit ACK for the DM: the broadcast is still in flight.
    auto ack = makeRoutingResponse(dmId, kRemoteNode);
    auto routing = makeRouting(meshtastic_Routing_Error_NONE);
    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(3, capture->count());
    expectEvent(2, dmId, kRemoteNode, TxAckState::ACKNOWLEDGED, meshtastic_Routing_Error_NONE, 1);

    // Implicit ACK for the broadcast: now nothing is outstanding.
    auto overheard = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    overheard.id = bcId;
    reliableShim->implicitAckForTest(&overheard);

    TEST_ASSERT_EQUAL_UINT32(4, capture->count());
    expectEvent(3, bcId, NODENUM_BROADCAST, TxAckState::ACKNOWLEDGED, meshtastic_Routing_Error_NONE, 0);
    TEST_ASSERT_EQUAL_UINT8(0, reliableShim->outstandingOwn());
}

void test_failure_of_one_send_leaves_the_other_outstanding(void)
{
    auto a = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    auto b = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kThirdNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(a, 1); // due on the next pass
    reliableShim->seedRetry(b, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    capture->clear();

    reliableShim->makeRetryDue(kLocalNode, a.id);
    reliableShim->runDueRetries();

    TEST_ASSERT_EQUAL_UINT32(1, capture->count());
    expectEvent(0, a.id, kRemoteNode, TxAckState::FAILED, meshtastic_Routing_Error_MAX_RETRANSMIT, 1);
    TEST_ASSERT_TRUE_MESSAGE(reliableShim->hasPending(kLocalNode, b.id), "the other send must survive");
    TEST_ASSERT_EQUAL_UINT8(1, reliableShim->outstandingOwn());
}

void test_relayed_records_do_not_count_toward_outstanding(void)
{
    // countOutstandingOwnTx() is what `outstanding` reports; relayed records share the same map.
    auto ours = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    auto foreignA = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kThirdNode, 1, /*wantAck=*/true);
    auto foreignB = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kThirdNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(ours, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    reliableShim->seedRetry(foreignA, NextHopRouter::NUM_INTERMEDIATE_RETX);
    reliableShim->seedRetry(foreignB, NextHopRouter::NUM_INTERMEDIATE_RETX);

    TEST_ASSERT_EQUAL_UINT32(3, reliableShim->pendingCount());
    TEST_ASSERT_EQUAL_UINT8(1, reliableShim->outstandingOwn());
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    airTimeFixture = std::make_unique<ScopedAirTimeFixture>();
    mockNodeDB = new MockNodeDB();
    nodeDB = mockNodeDB;
    reliableShim = new ReliableRouterTestShim();

    auto capturingRadio = std::make_unique<CaptureRadio>();
    radio = capturingRadio.get();
    reliableShim->addInterface(std::move(capturingRadio));

    mockRoutingModule = new MockRoutingModule();
    routingModule = mockRoutingModule;

    capture = new TxAckCapture();
    capture->observe(&txAckStatusObservable);

    printf("\n=== reliable send enters PENDING ===\n");
    RUN_TEST(test_reliable_dm_send_publishes_pending);
    RUN_TEST(test_reliable_broadcast_send_publishes_pending);
    RUN_TEST(test_unreliable_send_publishes_nothing);
    RUN_TEST(test_relayed_want_ack_send_publishes_nothing);

    printf("\n=== acknowledgement (implicit and explicit) ===\n");
    RUN_TEST(test_implicit_ack_publishes_acknowledged);
    RUN_TEST(test_implicit_ack_via_mqtt_keeps_pending);
    RUN_TEST(test_explicit_routing_ack_publishes_acknowledged);
    RUN_TEST(test_explicit_nak_publishes_failed_with_reason);

    printf("\n=== an outstanding send must not be disturbed ===\n");
    RUN_TEST(test_retransmission_publishes_nothing_and_keeps_pending);
    RUN_TEST(test_unrelated_inbound_packet_keeps_pending);
    RUN_TEST(test_ack_for_another_packet_keeps_pending);
    RUN_TEST(test_overheard_foreign_packet_keeps_pending);

    printf("\n=== failure ===\n");
    RUN_TEST(test_retry_exhaustion_publishes_failed);
    RUN_TEST(test_exhaustion_of_relayed_packet_publishes_nothing);
    RUN_TEST(test_rejected_send_publishes_failed);
    RUN_TEST(test_iface_refusal_reports_failed_without_a_bogus_reason);

    printf("\n=== several reliable sends in flight ===\n");
    RUN_TEST(test_two_outstanding_sends_resolve_independently);
    RUN_TEST(test_failure_of_one_send_leaves_the_other_outstanding);
    RUN_TEST(test_relayed_records_do_not_count_toward_outstanding);

    int result = UNITY_END();
    airTimeFixture.reset();
    exit(result);
}

void loop() {}
