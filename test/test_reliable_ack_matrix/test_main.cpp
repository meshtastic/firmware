// ReliableRouter ACK/NAK decision matrix: which ACK or NAK sniffReceived() emits per inbound
// shape, retransmission bookkeeping, the #11502 implicit ACK for our own overheard opaque DM
// (Group 5b drives the real OPAQUE_RELAY_ONLY ingress path, 5c binds it to our PKI ciphertext), and
// the pending-timer extensions.
// Harness copied from test_nexthop_routing (ReliableRouterTestShim + MockRoutingModule).

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/ReliableRouter.h"
#include "mesh/Throttle.h"
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
// MockNodeDB - inject sender records with a controlled public-key size, so the PKI_UNKNOWN_PUBKEY
// vs NO_CHANNEL discrimination in sniffReceived() can be driven per test.
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

    void addNode(NodeNum num, uint8_t publicKeySize = 0)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        node.last_heard = getTime();
        node.public_key.size = publicKeySize;
        if (publicKeySize)
            memset(node.public_key.bytes, 0x5C, publicKeySize);
        nodeInfoLiteSetBit(&node, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

// ---------------------------------------------------------------------------
// Test shim - expose the protected sniff/filter entry points and the pending/route-health state.
// ---------------------------------------------------------------------------
class ReliableRouterTestShim : public ReliableRouter
{
  public:
    ReliableRouterTestShim() : ReliableRouter() {}

    using NextHopRouter::findRouteHealth;
    using NextHopRouter::noteRouteFailure;
    using NextHopRouter::noteRouteLearned;

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

    bool filterForTest(const meshtastic_MeshPacket *p) { return ReliableRouter::shouldFilterReceived(p); }

    bool hasPending(NodeNum from, PacketId id) { return findPendingPacket(from, id) != nullptr; }

    uint32_t pendingNextTx(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        TEST_ASSERT_NOT_NULL(entry);
        return entry->nextTxMsec;
    }

    const meshtastic_MeshPacket *pendingPacket(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        TEST_ASSERT_NOT_NULL(entry);
        return entry->packet;
    }

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

// Capture radio with a configurable per-packet airtime, so the pending-timer extension loops
// (which are no-ops with a 0-returning stub) become observable.
class TimedCaptureRadio : public RadioInterface
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
        return packetTimeMsec;
    }

    void reset()
    {
        sentPackets.clear();
        cancelCount = 0;
        packetTimeMsec = 0;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
    uint32_t cancelCount = 0;
    uint32_t packetTimeMsec = 0;
};

class MockRoutingModule : public RoutingModule
{
  public:
    // The relaying copy the caller handed us, flattened to the fields allocAckNak() forwards onto
    // the ack. One entry per sendAckNak() call, so it stays aligned with ackNaks.
    struct RelaySource {
        bool present;
        uint8_t relayNode;
        bool hasRxRssi;
        int32_t rxRssi;
        float rxSnr;
    };

    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit = 0,
                    bool ackWantsAck = false, const meshtastic_MeshPacket *relaySource = nullptr) override
    {
        ackNaks.emplace_back(err, to, idFrom, chIndex, hopLimit, ackWantsAck);
        if (relaySource)
            relaySources.push_back(
                {true, relaySource->relay_node, relaySource->has_rx_rssi, relaySource->rx_rssi, relaySource->rx_snr});
        else
            relaySources.push_back({false, NO_RELAY_NODE, false, 0, 0.0f});
    }

    std::list<std::tuple<meshtastic_Routing_Error, NodeNum, PacketId, ChannelIndex, uint8_t, bool>> ackNaks;
    std::vector<RelaySource> relaySources;
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
static TimedCaptureRadio *radio = nullptr;
static MockRoutingModule *mockRoutingModule = nullptr;

static std::unique_ptr<ScopedAirTimeFixture> airTimeFixture;
static PacketId nextTestPacketId = 0x7A000000;

/// Send `p` through the router for real - send() encodes it, records what a relay copy must carry, and
/// starts the retransmission - and return the frame as it left the radio. The wire form, not a stand-in.
static meshtastic_MeshPacket seedViaSend(const meshtastic_MeshPacket &p)
{
    auto *copy = packetPool.allocCopy(p);
    TEST_ASSERT_NOT_NULL(copy);
    const size_t before = radio->sentPackets.size();
    TEST_ASSERT_EQUAL_INT(ERRNO_OK, reliableShim->send(copy));
    TEST_ASSERT_EQUAL_MESSAGE(before + 1, radio->sentPackets.size(), "send() must hand exactly one frame to the radio");
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, radio->sentPackets.back().which_payload_variant);
    return radio->sentPackets.back();
}

/// The frame a neighbour repeats: our bytes, one hop spent, its own relay_node and link quality.
static meshtastic_MeshPacket relayedCopyOf(const meshtastic_MeshPacket &wire, uint8_t relayNode,
                                           meshtastic_MeshPacket_TransportMechanism transport)
{
    meshtastic_MeshPacket p = wire;
    p.hop_limit = wire.hop_limit > 0 ? wire.hop_limit - 1 : 0;
    p.relay_node = relayNode;
    p.transport_mechanism = transport;
    p.has_rx_rssi = true;
    p.rx_rssi = -87;
    p.rx_snr = 6.25f;
    return p;
}

/// Forget the channel a frame was sent on, so our own frame is as unreadable to us as a PKI DM we
/// originated: the wire hash matches nothing we hold and ingress classifies it opaque.
static void forgetChannelOf(const meshtastic_MeshPacket &wire, ChannelIndex sentOn = 1)
{
    meshtastic_Channel &ch = channels.getByIndex(sentOn);
    ch.settings.psk.bytes[0] ^= 0xFF;
    channels.onConfigChanged();
    bool stillHeld = false;
    for (ChannelIndex i = 0; i < channels.getNumChannels(); i++)
        if (channels.getHash(i) == wire.channel)
            stillHeld = true;
    TEST_ASSERT_FALSE_MESSAGE(stillHeld, "fixture: the wire hash must no longer match a held channel");
}

/// Give us a keypair and the remote a known public key, so a DM to it goes out PKI-encrypted for real.
static void installPkiPeer(NodeNum peer)
{
    uint8_t ourPub[32], ourPriv[32], peerPub[32], peerPriv[32];
    crypto->generateKeyPair(ourPub, ourPriv);
    crypto->generateKeyPair(peerPub, peerPriv);
    crypto->setDHPrivateKey(ourPriv);
    config.security.private_key.size = 32;
    memcpy(config.security.private_key.bytes, ourPriv, 32);
    owner.public_key.size = 32;
    memcpy(owner.public_key.bytes, ourPub, 32);
    mockNodeDB->addNode(peer, 32);
    memcpy(mockNodeDB->testNodes.back().public_key.bytes, peerPub, 32);
}

/// send() a DM that must leave as PKI: channel byte 0 and flagged, the shape our own relayed copy comes
/// back in - opaque to us, since only the recipient holds the key.
static meshtastic_MeshPacket seedPkiDmViaSend(const meshtastic_MeshPacket &p)
{
    const auto wire = seedViaSend(p);
    TEST_ASSERT_TRUE_MESSAGE(wire.pki_encrypted, "fixture: the DM must have gone out PKI-encrypted");
    TEST_ASSERT_EQUAL_UINT8(0, wire.channel);
    return wire;
}

/// What a relay that holds the channel key puts back on the air after touching the payload, as a
/// traceroute hop does: `decoded` re-encoded and re-encrypted under the same channel, our header kept.
static meshtastic_MeshPacket reencodedRelayOf(const meshtastic_MeshPacket &decoded, const meshtastic_MeshPacket &wire,
                                              uint8_t relayNode)
{
    meshtastic_MeshPacket p = relayedCopyOf(wire, relayNode, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1];
    size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &decoded.decoded);
    TEST_ASSERT_NOT_EQUAL(0, numbytes);
    TEST_ASSERT_EQUAL_INT(wire.channel, channels.setActiveByIndex(decoded.channel));
    crypto->encryptPacket(decoded.from, decoded.id, numbytes, bytes);
    memcpy(p.encrypted.bytes, bytes, numbytes);
    p.encrypted.size = numbytes;
    return p;
}

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

static meshtastic_MeshPacket makeDecodedPacket(meshtastic_PortNum portnum, NodeNum from, NodeNum to, uint8_t channel,
                                               bool wantAck = false)
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

static meshtastic_MeshPacket makeEncryptedToUs(uint8_t channel, bool wantAck)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = kRemoteNode;
    p.to = kLocalNode;
    p.id = nextTestPacketId++;
    p.channel = channel;
    p.hop_start = 3;
    p.hop_limit = 3;
    p.relay_node = 0x22;
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    p.want_ack = wantAck;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 32;
    return p;
}

static void expectSingleAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId id, ChannelIndex chIndex, uint8_t hopLimit,
                               bool ackWantsAck)
{
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->ackNaks.size());
    const auto &ack = mockRoutingModule->ackNaks.front();
    TEST_ASSERT_EQUAL_INT(err, std::get<0>(ack));
    TEST_ASSERT_EQUAL_HEX32(to, std::get<1>(ack));
    TEST_ASSERT_EQUAL_HEX32(id, std::get<2>(ack));
    TEST_ASSERT_EQUAL_UINT8(chIndex, std::get<3>(ack));
    TEST_ASSERT_EQUAL_UINT8(hopLimit, std::get<4>(ack));
    TEST_ASSERT_EQUAL(ackWantsAck, std::get<5>(ack));
}

// #10767: only the implicit ack for an overheard rebroadcast of our own packet carries a relay
// source; every other ACK/NAK must leave it unset so the phone is never told a relayer we did not
// hear.
static void expectRelaySource(uint8_t relayNode, int32_t rxRssi, float rxSnr)
{
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->relaySources.size());
    const auto &relay = mockRoutingModule->relaySources.front();
    TEST_ASSERT_TRUE(relay.present);
    TEST_ASSERT_EQUAL_HEX8(relayNode, relay.relayNode);
    TEST_ASSERT_TRUE(relay.hasRxRssi);
    TEST_ASSERT_EQUAL_INT32(rxRssi, relay.rxRssi);
    TEST_ASSERT_EQUAL_FLOAT(rxSnr, relay.rxSnr);
}

static void expectNoRelaySource()
{
    TEST_ASSERT_EQUAL_UINT32(1, mockRoutingModule->relaySources.size());
    TEST_ASSERT_FALSE(mockRoutingModule->relaySources.front().present);
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
    config.lora.hop_limit = 3; // keep getHopLimitForResponse() deterministic across tests
    config.security.private_key.size = 0;
    owner.is_licensed = false;
    // No keypair of our own unless a test installs one (installPkiPeer), so DMs go out on the channel by default.
    owner.public_key.size = 0;
    mockNodeDB->clearTestNodes();
    reliableShim->clearPendingForTest();
    reliableShim->resetRouteHealthForTest();
    radio->reset();
    mockRoutingModule->ackNaks.clear();
    mockRoutingModule->relaySources.clear();
    configureChannels();
}

void tearDown(void) {}

// ===========================================================================
// Group 1 - want_ack ACK variants (decoded packets to us)
// ===========================================================================

void test_text_dm_want_ack_gets_want_ack_ack(void)
{
    auto p = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    p.relay_node = 0x77; // this ACK travels the mesh for someone else's DM; it must claim no relayer
    p.has_rx_rssi = true;
    p.rx_rssi = -55;
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);
    TEST_ASSERT_NOT_EQUAL(0, expectedHop); // must be distinguishable from the 0-hop ACK branch

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, expectedHop, /*ackWantsAck=*/true);
    expectNoRelaySource();
}

void test_text_reply_still_gets_want_ack_ack(void)
{
    // shouldSuccessAckWithWantAck() runs before the response branch, so a text DM that is itself a
    // reply still gets the reliable want-ack ACK (not the 0-hop response treatment).
    auto p = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    p.decoded.reply_id = 0x1234;
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, expectedHop, /*ackWantsAck=*/true);
}

void test_nontext_dm_want_ack_gets_plain_ack(void)
{
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);
    TEST_ASSERT_NOT_EQUAL(0, expectedHop);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, expectedHop, /*ackWantsAck=*/false);
}

void test_response_heard_directly_gets_zero_hop_ack(void)
{
    // A response (request_id set) heard at 0 hops: the original sender cannot overhear an implicit
    // ACK, so we ACK - but only with hop limit 0.
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    p.decoded.request_id = 0x4242;

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
}

void test_response_relayed_gets_no_ack(void)
{
    // A relayed response with no next-hop addressing already got its implicit ACK from the
    // rebroadcast; ACKing again would only burn airtime.
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    p.decoded.request_id = 0x4242;
    p.hop_limit = 2; // hop_start 3 -> 1 hop away

    reliableShim->sniffForTest(&p, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

void test_response_relayed_via_next_hop_gets_zero_hop_ack(void)
{
    // Relayed, but directed at a next_hop: the immediate relayer retransmits until stopped, so a
    // 0-hop ACK is still required.
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/true);
    p.decoded.request_id = 0x4242;
    p.hop_limit = 2;
    p.next_hop = 0x77;

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
}

void test_broadcast_want_ack_gets_no_ack(void)
{
    // 0-hop reliability is unicast-only: a want_ack broadcast is never ACKed (isToUs() is false).
    auto p = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kRemoteNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);

    reliableShim->sniffForTest(&p, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

// ===========================================================================
// Group 2 - undecodable want_ack NAKs (encrypted packets to us)
// ===========================================================================

void test_pki_unknown_sender_gets_pki_unknown_pubkey_nak(void)
{
    // channel==0 + sender absent from NodeDB -> the PKI key-amnesia NAK, on the primary channel.
    auto p = makeEncryptedToUs(/*channel=*/0, /*wantAck=*/true);
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, kRemoteNode, p.id, channels.getPrimaryIndex(), expectedHop,
                       /*ackWantsAck=*/false);
}

void test_pki_keyless_sender_record_gets_pki_unknown_pubkey_nak(void)
{
    // The sender is in the DB but we hold no key for it - same NAK as a fully unknown node.
    mockNodeDB->addNode(kRemoteNode, /*publicKeySize=*/0);
    auto p = makeEncryptedToUs(/*channel=*/0, /*wantAck=*/true);
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, kRemoteNode, p.id, channels.getPrimaryIndex(), expectedHop,
                       /*ackWantsAck=*/false);
}

void test_pki_known_key_sender_gets_no_channel_nak(void)
{
    // Discriminator: with the sender's key on hand an undecodable channel-0 want_ack packet is NOT a
    // key problem, so it falls through to the generic NO_CHANNEL NAK.
    mockNodeDB->addNode(kRemoteNode, /*publicKeySize=*/32);
    auto p = makeEncryptedToUs(/*channel=*/0, /*wantAck=*/true);
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NO_CHANNEL, kRemoteNode, p.id, channels.getPrimaryIndex(), expectedHop,
                       /*ackWantsAck=*/false);
}

void test_unknown_channel_hash_gets_no_channel_nak(void)
{
    // Nonzero channel hash we cannot decode -> NO_CHANNEL on the primary channel (not the hash).
    auto p = makeEncryptedToUs(/*channel=*/0x5A, /*wantAck=*/true);
    uint8_t expectedHop = mockRoutingModule->getHopLimitForResponse(p);

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NO_CHANNEL, kRemoteNode, p.id, channels.getPrimaryIndex(), expectedHop,
                       /*ackWantsAck=*/false);
}

// ===========================================================================
// Group 3 - no want_ack, but we are the addressed next hop
// ===========================================================================

void test_next_hop_addressed_to_us_gets_zero_hop_ack(void)
{
    // We were the addressed next hop: a 0-hop ACK stops the relayer's retransmissions even though
    // the packet itself did not ask for an ACK.
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/false);
    p.next_hop = 0x11; // our last byte
    p.hop_limit = 1;

    reliableShim->sniffForTest(&p, nullptr);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kRemoteNode, p.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
}

void test_next_hop_with_hop_limit_zero_gets_no_ack(void)
{
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/false);
    p.next_hop = 0x11;
    p.hop_limit = 0;

    reliableShim->sniffForTest(&p, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

void test_next_hop_other_byte_gets_no_ack(void)
{
    auto p = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/false);
    p.next_hop = 0x22; // someone else's byte
    p.hop_limit = 1;

    reliableShim->sniffForTest(&p, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
}

// ===========================================================================
// Group 4 - explicit ACK/NAK vs pending retransmissions, MQTT gate, route health
// ===========================================================================

void test_explicit_ack_stops_retransmissions_and_clears_route_failures(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    reliableShim->noteRouteLearned(kRemoteNode, 0xAB, millis());
    reliableShim->noteRouteFailure(kRemoteNode);
    reliableShim->noteRouteFailure(kRemoteNode);
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());

    auto ack = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode, kLocalNode, 1);
    ack.decoded.request_id = original.id;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
    // The end-to-end ACK proves the route to its sender works -> noteRouteSuccess clears failures.
    RouteHealth *h = reliableShim->findRouteHealth(kRemoteNode);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_UINT8(0, h->consecutiveFailures);
}

void test_nak_stops_retransmissions_but_keeps_route_failures(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    reliableShim->noteRouteLearned(kRemoteNode, 0xAB, millis());
    reliableShim->noteRouteFailure(kRemoteNode);
    reliableShim->noteRouteFailure(kRemoteNode);

    auto nak = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode, kLocalNode, 1);
    nak.decoded.request_id = original.id;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.error_reason = meshtastic_Routing_Error_MAX_RETRANSMIT;

    reliableShim->sniffForTest(&nak, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
    // A NAK is not a delivery success: the failure count must survive.
    RouteHealth *h = reliableShim->findRouteHealth(kRemoteNode);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_UINT8(2, h->consecutiveFailures);
}

void test_pki_unknown_pubkey_nak_stops_retransmissions(void)
{
    // The remote lost our key: its PKI_UNKNOWN_PUBKEY NAK must still clear the pending record.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    auto nak = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode, kLocalNode, 1);
    nak.decoded.request_id = original.id;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.error_reason = meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;

    reliableShim->sniffForTest(&nak, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_own_ack_echo_via_mqtt_keeps_retransmissions(void)
{
    // An implicit ACK that is our own traffic echoed back via MQTT must not stop LoRa retries.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    auto echo = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kLocalNode, kLocalNode, 1);
    echo.decoded.request_id = original.id;
    echo.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;

    reliableShim->sniffForTest(&echo, nullptr);

    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
    TEST_ASSERT_TRUE(reliableShim->hasPending(kLocalNode, original.id));
}

void test_own_ack_echo_via_lora_stops_retransmissions(void)
{
    // Control for the MQTT gate: the identical from-us echo via LoRa does stop the retries.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    auto echo = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kLocalNode, kLocalNode, 1);
    echo.decoded.request_id = original.id;
    echo.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;

    reliableShim->sniffForTest(&echo, nullptr);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_remote_ack_via_mqtt_still_stops_retransmissions(void)
{
    // The gate is scoped to from-us echoes: a genuine end-to-end ACK arriving over MQTT counts.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    auto ack = makeDecodedPacket(meshtastic_PortNum_ROUTING_APP, kRemoteNode, kLocalNode, 1);
    ack.decoded.request_id = original.id;
    ack.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.error_reason = meshtastic_Routing_Error_NONE;

    reliableShim->sniffForTest(&ack, &routing);

    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

// ===========================================================================
// Group 5 - implicit ACK for our own overheard DM through shouldFilterReceived. This is the
// pre-existing route (a decodable copy still in encrypted wire form reaches it); the #11502
// opaque short-circuit is exercised separately in Group 5b.
// ===========================================================================

void test_overheard_own_dm_rebroadcast_mints_implicit_ack(void)
{
    // A neighbour repeats the frame exactly as we sent it, still encrypted; the LoRa copy mints the
    // ACK and stops the retransmissions.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    mockRoutingModule->ackNaks.clear();

    auto overheard = relayedCopyOf(wire, 0x99, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    reliableShim->filterForTest(&overheard);

    // ACK is addressed to us (so it reaches the phone) on the pending copy's channel.
    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    // The overheard copy is the relay source, so the phone learns who relayed and at what quality.
    expectRelaySource(0x99, -87, 6.25f);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_overheard_own_dm_via_mqtt_acks_but_keeps_retransmissions(void)
{
    // The MQTT copy still surfaces "Delivered to mesh" but must not cancel the LoRa retries.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    mockRoutingModule->ackNaks.clear();

    auto overheard = relayedCopyOf(wire, 0x99, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT);
    reliableShim->filterForTest(&overheard);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

void test_overheard_foreign_packet_mints_no_implicit_ack(void)
{
    // Someone else's traffic must never mint an ACK, even with a colliding packet id.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    meshtastic_MeshPacket foreign = meshtastic_MeshPacket_init_zero;
    foreign.from = kRemoteNode;
    foreign.to = kThirdNode;
    foreign.id = original.id;
    foreign.hop_start = 3;
    foreign.hop_limit = 2;
    foreign.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    foreign.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    foreign.encrypted.size = 32;

    reliableShim->filterForTest(&foreign);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

// ===========================================================================
// Group 5b - the real #11502 wiring: an overheard own DM under a channel hash we cannot decode
// (a PKI DM we sent) is OPAQUE_RELAY_ONLY in Router::perhapsHandleReceived and returns BEFORE
// shouldFilterReceived; the fix is the isFromUs branch there. Driven through the public ingress
// queue (enqueueReceivedMessage + runOnce), so deleting that branch fails these tests.
// ===========================================================================

// An encrypted copy of our own DM under an unknown channel hash: not to us (no PKI attempt), no
// hash match -> DECODE_OPAQUE -> OPAQUE_RELAY_ONLY. hop_limit > 0 so the opaque relay does not
// short-circuit before the ACK branch.
static meshtastic_MeshPacket makeOpaqueOwnOverheard(PacketId id, meshtastic_MeshPacket_TransportMechanism transport)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = kLocalNode;
    p.to = kRemoteNode;
    p.id = id;
    p.channel = 0x5A;
    p.hop_start = 3;
    p.hop_limit = 2;
    p.transport_mechanism = transport;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 32;
    memset(p.encrypted.bytes, 0xC3, p.encrypted.size);
    p.relay_node = 0x4D;
    p.has_rx_rssi = true;
    p.rx_rssi = -112;
    p.rx_snr = -3.5f;
    return p;
}

static void ingressOverheard(const meshtastic_MeshPacket &p)
{
    meshtastic_MeshPacket *copy = packetPool.allocCopy(p);
    TEST_ASSERT_NOT_NULL(copy);
    reliableShim->enqueueReceivedMessage(copy);
    reliableShim->runOnce();
}

void test_ingress_opaque_own_dm_lora_mints_implicit_ack_and_stops_retries(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    mockRoutingModule->ackNaks.clear();
    forgetChannelOf(wire);

    ingressOverheard(relayedCopyOf(wire, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA));

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    // Relay attribution survives the opaque short-circuit too: the header fields are all it needs.
    expectRelaySource(0x4D, -87, 6.25f);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_ingress_opaque_own_dm_mqtt_acks_but_keeps_retries(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    mockRoutingModule->ackNaks.clear();
    forgetChannelOf(wire);

    ingressOverheard(relayedCopyOf(wire, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT));

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

void test_ingress_opaque_foreign_packet_mints_no_implicit_ack(void)
{
    // The isFromUs guard on the opaque branch: someone else's opaque traffic with a colliding id
    // is relayed but never ACKed.
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(original, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);

    auto foreign = makeOpaqueOwnOverheard(original.id, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    foreign.from = kRemoteNode;
    foreign.to = kThirdNode;
    ingressOverheard(foreign);

    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());
}

// ===========================================================================
// Group 5c - the implicit ACK for a PKI DM is bound to our ciphertext. Its header is cleartext anyone
// can copy, but no relay can alter a PKI payload, so a genuine relayed copy carries our exact bytes
// and a copy with our (from,id) but other bytes is a forgery. Real PKI DMs through send(), driven
// through the public ingress queue, where our own PKI DM is opaque to us.
// ===========================================================================

static meshtastic_MeshPacket makePkiDm()
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 0, /*wantAck=*/true);
    // A real-length text so the ciphertext is a wire-sized frame, not the fixture's two-byte Data.
    static const char text[] = "meet at the north gate at six, bring the spare radio";
    original.decoded.payload.size = sizeof(text) - 1;
    memcpy(original.decoded.payload.bytes, text, original.decoded.payload.size);
    return original;
}

void test_ingress_relayed_copy_of_own_pki_dm_mints_implicit_ack(void)
{
    installPkiPeer(kRemoteNode);
    auto original = makePkiDm();
    const auto wire = seedPkiDmViaSend(original);
    mockRoutingModule->ackNaks.clear();

    ingressOverheard(relayedCopyOf(wire, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA));

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 0, /*hopLimit=*/0, /*ackWantsAck=*/false);
    expectRelaySource(0x4D, -87, 6.25f);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

void test_ingress_forged_copy_of_own_pki_dm_mints_no_implicit_ack(void)
{
    installPkiPeer(kRemoteNode);
    auto original = makePkiDm();
    const auto wire = seedPkiDmViaSend(original);
    TEST_ASSERT_TRUE(wire.encrypted.size > original.decoded.payload.size);
    mockRoutingModule->ackNaks.clear();

    auto forged = relayedCopyOf(wire, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    forged.encrypted.bytes[wire.encrypted.size / 2] ^= 0x01; // same length, one bit off, mid-frame
    ingressOverheard(forged);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());

    auto shorter = relayedCopyOf(wire, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA);
    shorter.encrypted.size = wire.encrypted.size - 1;
    ingressOverheard(shorter);
    TEST_ASSERT_EQUAL_UINT32(0, mockRoutingModule->ackNaks.size());
    TEST_ASSERT_EQUAL_UINT32(1, reliableShim->pendingCount());

    // The retransmission the forgery tried to stop is still scheduled and still goes out.
    reliableShim->makeRetryDue(kLocalNode, original.id);
    reliableShim->runDueRetries();
    TEST_ASSERT_EQUAL_UINT32(2, radio->sentPackets.size());
}

// PKI encryption draws a fresh nonce per encode, so a retry is a different frame. The record must
// follow it: a relay of the retry is what a neighbour will actually repeat.
void test_ingress_relayed_copy_of_own_pki_retry_mints_implicit_ack(void)
{
    installPkiPeer(kRemoteNode);
    auto original = makePkiDm();
    const auto first = seedPkiDmViaSend(original);
    mockRoutingModule->ackNaks.clear();

    reliableShim->makeRetryDue(kLocalNode, original.id);
    reliableShim->runDueRetries();
    TEST_ASSERT_EQUAL_UINT32(2, radio->sentPackets.size());
    const auto retry = radio->sentPackets.back();
    TEST_ASSERT_TRUE(retry.pki_encrypted);
    TEST_ASSERT_EQUAL_UINT8(first.channel, retry.channel);
    TEST_ASSERT_EQUAL_UINT32(first.encrypted.size, retry.encrypted.size);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first.encrypted.bytes, retry.encrypted.bytes, first.encrypted.size));

    ingressOverheard(relayedCopyOf(retry, 0x4D, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA));
    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 0, /*hopLimit=*/0, /*ackWantsAck=*/false);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

// A relay that holds the channel key rebroadcasts what it decoded, and may change it on the way: a
// traceroute relay appends its hop, an older firmware drops fields it does not know. Channel traffic
// therefore keeps the header match; only a PKI payload is bound to bytes.
void test_ingress_reencoded_relay_of_own_channel_dm_mints_implicit_ack(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TRACEROUTE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    TEST_ASSERT_FALSE(wire.pki_encrypted);
    mockRoutingModule->ackNaks.clear();

    auto altered = original;
    altered.decoded.payload.bytes[altered.decoded.payload.size++] = 0x4D; // the relay wrote itself in
    auto relayed = reencodedRelayOf(altered, wire, 0x4D);
    TEST_ASSERT_NOT_EQUAL(wire.encrypted.size, relayed.encrypted.size);
    ingressOverheard(relayed);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

// Portduino's SimRadio and the local send path hand the router copies that are already decoded, so
// there is no ciphertext to check: a decoded overheard copy of our own DM is matched on the header
// alone, as before. Without this the simulator never sees "Delivered to mesh".
void test_ingress_decoded_copy_of_own_dm_mints_implicit_ack_on_header(void)
{
    auto original = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    const auto wire = seedViaSend(original);
    mockRoutingModule->ackNaks.clear();

    meshtastic_MeshPacket decodedCopy = original; // as SimRadio delivers it: plaintext, our header
    decodedCopy.hop_limit = wire.hop_limit - 1;
    decodedCopy.relay_node = 0x4D;
    decodedCopy.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    ingressOverheard(decodedCopy);

    expectSingleAckNak(meshtastic_Routing_Error_NONE, kLocalNode, original.id, 1, /*hopLimit=*/0, /*ackWantsAck=*/false);
    TEST_ASSERT_EQUAL_UINT32(0, reliableShim->pendingCount());
}

// ===========================================================================
// Group 6 - pending-timer airtime extension in send() and shouldFilterReceived()
// ===========================================================================

void test_send_extends_other_pending_deadlines_not_own(void)
{
    // While we transmit packet B we cannot hear an (implicit) ACK for pending A, so A's deadline
    // must move out by B's airtime. B's own fresh record must not be self-extended.
    radio->packetTimeMsec = 50000; // dwarfs any real time elapsed inside the test

    auto a = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(a, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    uint32_t aBefore = reliableShim->pendingNextTx(kLocalNode, a.id);

    auto b = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, NODENUM_BROADCAST, 0, /*wantAck=*/true);
    auto *allocated = packetPool.allocCopy(b);
    TEST_ASSERT_NOT_NULL(allocated);
    TEST_ASSERT_EQUAL_INT(ERRNO_OK, reliableShim->send(allocated));

    TEST_ASSERT_EQUAL_UINT32(2, reliableShim->pendingCount());
    TEST_ASSERT_EQUAL_UINT32(aBefore + 50000, reliableShim->pendingNextTx(kLocalNode, a.id));

    // B's deadline is millis-at-set + getRetransmissionMsec(B); a self-extension would push it a
    // further 50s out, past anything the wall clock could account for.
    uint32_t bTx = reliableShim->pendingNextTx(kLocalNode, b.id);
    uint32_t retrans = radio->getRetransmissionMsec(reliableShim->pendingPacket(kLocalNode, b.id));
    // Via Throttle rather than a bare millis() compare, per the house deadline rule.
    TEST_ASSERT_TRUE_MESSAGE(Throttle::deadlinePassed(bTx - retrans), "own record must not be extended by its own send");
}

void test_receive_extends_all_pending_deadlines(void)
{
    // While receiving any packet we cannot hear an ACK either: every pending deadline moves out by
    // the received packet's airtime.
    radio->packetTimeMsec = 40000;

    auto a = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kRemoteNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(a, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    auto b = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, kLocalNode, kThirdNode, 1, /*wantAck=*/true);
    reliableShim->seedRetry(b, NextHopRouter::NUM_RELIABLE_UNICAST_ATTEMPTS);
    uint32_t aBefore = reliableShim->pendingNextTx(kLocalNode, a.id);
    uint32_t bBefore = reliableShim->pendingNextTx(kLocalNode, b.id);

    auto inbound = makeDecodedPacket(meshtastic_PortNum_TELEMETRY_APP, kRemoteNode, kLocalNode, 1, /*wantAck=*/false);
    reliableShim->filterForTest(&inbound);

    TEST_ASSERT_EQUAL_UINT32(aBefore + 40000, reliableShim->pendingNextTx(kLocalNode, a.id));
    TEST_ASSERT_EQUAL_UINT32(bBefore + 40000, reliableShim->pendingNextTx(kLocalNode, b.id));
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

    auto capture = std::make_unique<TimedCaptureRadio>();
    radio = capture.get();
    reliableShim->addInterface(std::move(capture));

    mockRoutingModule = new MockRoutingModule();
    routingModule = mockRoutingModule;

    printf("\n=== want_ack ACK variants ===\n");
    RUN_TEST(test_text_dm_want_ack_gets_want_ack_ack);
    RUN_TEST(test_text_reply_still_gets_want_ack_ack);
    RUN_TEST(test_nontext_dm_want_ack_gets_plain_ack);
    RUN_TEST(test_response_heard_directly_gets_zero_hop_ack);
    RUN_TEST(test_response_relayed_gets_no_ack);
    RUN_TEST(test_response_relayed_via_next_hop_gets_zero_hop_ack);
    RUN_TEST(test_broadcast_want_ack_gets_no_ack);

    printf("\n=== undecodable want_ack NAKs ===\n");
    RUN_TEST(test_pki_unknown_sender_gets_pki_unknown_pubkey_nak);
    RUN_TEST(test_pki_keyless_sender_record_gets_pki_unknown_pubkey_nak);
    RUN_TEST(test_pki_known_key_sender_gets_no_channel_nak);
    RUN_TEST(test_unknown_channel_hash_gets_no_channel_nak);

    printf("\n=== next-hop 0-hop ACK without want_ack ===\n");
    RUN_TEST(test_next_hop_addressed_to_us_gets_zero_hop_ack);
    RUN_TEST(test_next_hop_with_hop_limit_zero_gets_no_ack);
    RUN_TEST(test_next_hop_other_byte_gets_no_ack);

    printf("\n=== ACK/NAK vs pending retransmissions ===\n");
    RUN_TEST(test_explicit_ack_stops_retransmissions_and_clears_route_failures);
    RUN_TEST(test_nak_stops_retransmissions_but_keeps_route_failures);
    RUN_TEST(test_pki_unknown_pubkey_nak_stops_retransmissions);
    RUN_TEST(test_own_ack_echo_via_mqtt_keeps_retransmissions);
    RUN_TEST(test_own_ack_echo_via_lora_stops_retransmissions);
    RUN_TEST(test_remote_ack_via_mqtt_still_stops_retransmissions);

    printf("\n=== implicit ACK for our own overheard DM ===\n");
    RUN_TEST(test_overheard_own_dm_rebroadcast_mints_implicit_ack);
    RUN_TEST(test_overheard_own_dm_via_mqtt_acks_but_keeps_retransmissions);
    RUN_TEST(test_overheard_foreign_packet_mints_no_implicit_ack);

    printf("\n=== implicit ACK through the opaque ingress short-circuit (#11502) ===\n");
    RUN_TEST(test_ingress_opaque_own_dm_lora_mints_implicit_ack_and_stops_retries);
    RUN_TEST(test_ingress_opaque_own_dm_mqtt_acks_but_keeps_retries);
    RUN_TEST(test_ingress_opaque_foreign_packet_mints_no_implicit_ack);
    RUN_TEST(test_ingress_relayed_copy_of_own_pki_dm_mints_implicit_ack);
    RUN_TEST(test_ingress_forged_copy_of_own_pki_dm_mints_no_implicit_ack);
    RUN_TEST(test_ingress_relayed_copy_of_own_pki_retry_mints_implicit_ack);
    RUN_TEST(test_ingress_reencoded_relay_of_own_channel_dm_mints_implicit_ack);
    RUN_TEST(test_ingress_decoded_copy_of_own_dm_mints_implicit_ack_on_header);

    printf("\n=== pending-timer airtime extension ===\n");
    RUN_TEST(test_send_extends_other_pending_deadlines_not_own);
    RUN_TEST(test_receive_extends_all_pending_deadlines);

    int result = UNITY_END();
    airTimeFixture.reset();
    exit(result);
}

void loop() {}
