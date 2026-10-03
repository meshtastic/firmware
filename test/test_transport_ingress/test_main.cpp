// Unit tests for the ingress and egress rules every non-LoRa bearer shares:
// MeshTransportBase::sanitizeIngress() and MeshTransportBase::stripForTransmit() in
// src/mesh/MeshTransportBase.cpp, driven through each bearer's own ingress where it can be reached
// without a radio (UdpMulticastHandler::decodeIngress, BLEGattMeshHandler::deliverToRouter,
// BLEMeshHandler::deliverToRouter) and directly for MQTT, whose onReceiveProto path is covered end
// to end by test_mqtt. The advertisement bearer measures its own hop, so it alone reports rx_rssi.
//
// Why: a LoRa arrival carries only what the LoRa header carries, so every field outside it (tx_after,
// priority, pki_encrypted, public_key, rx_*) arrives at its default over radio. A bearer that hands
// over a whole MeshPacket lets the sender choose them: a sent priority MAX outranks the ACK ceiling
// and evicts one of ours from the TX queue, a sent tx_after schedules our transmit, a sent
// pki_encrypted claims PKI authentication the Router never established, and a sent rx_rssi reads as
// our own measurement. A local origin (from 0 or our own node number) reaches paths that trust
// isFromUs, remote admin among them, and an out-of-range hop count is not relayable.
//
// Regression guarded: the guards were once hand-copied into each bearer and drifted; UDP cleared
// neither tx_after nor priority while the BLE bearers did. Every test here runs once per bearer, so a
// bearer that stops calling the shared rules, or a rule dropped from them, fails by name. The strip
// tests pin that what a bearer transmits carries none of those fields, so an older receiver that
// clears less than this one still gets defaults, and that the LoRa-header fields a relay depends on
// (hop_limit, hop_start, via_mqtt, next_hop, relay_node, want_ack) survive both directions.

#include "TestUtil.h"
#include "mesh/MeshTransportBase.h"
#include "mesh/NodeDB.h"
#include "mesh/mesh-pb-constants.h"
#include <cstring>
#include <unity.h>

// main.h, not UdpMulticastHandler.h: the handler includes main.h, which names the handler type.
#include "main.h"
#if HAS_BLE_GATT_MESH
#include "mesh/BLEGattMeshHandler.h"
#endif
#if HAS_BLE_MESH
#include "mesh/BLEMeshHandler.h"
#endif
#include <vector>

namespace
{

constexpr NodeNum kSender = 0x3061b02e;

// The RSSI a bearer that measures its own hop reports for every fixture arrival.
constexpr int8_t kHopRssi = -42;

struct Bearer {
    const char *name;
    meshtastic_MeshPacket_TransportMechanism medium;
    // Delivers `sent` the way this bearer's ingress would. True, with `got` filled, when admitted.
    bool (*deliver)(const meshtastic_MeshPacket &sent, meshtastic_MeshPacket &got);
    // True when the bearer measures the hop it arrived on, so rx_rssi is kHopRssi rather than cleared.
    bool measuresHop;
};

#if HAS_UDP_MULTICAST
bool deliverUdp(const meshtastic_MeshPacket &sent, meshtastic_MeshPacket &got)
{
    uint8_t body[meshtastic_MeshPacket_size];
    const size_t n = pb_encode_to_bytes(body, sizeof(body), &meshtastic_MeshPacket_msg, &sent);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "fixture packet encodes");
    got = meshtastic_MeshPacket_init_zero;
    return UdpMulticastHandler::decodeIngress(body, n, got);
}
#endif

bool deliverMqtt(const meshtastic_MeshPacket &sent, meshtastic_MeshPacket &got)
{
    got = sent;
    return MeshTransportBase::sanitizeIngress(got, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT);
}

#if HAS_BLE_GATT_MESH
// The GATT handler with its BLE stack stubbed out, reached at the point a reassembled packet arrives.
class GattIngress : public BLEGattMeshHandler
{
  public:
    std::vector<meshtastic_MeshPacket> received;

    void start() override { isRunning = true; }
    void stop() override { isRunning = false; }
    void deliver(const uint8_t *data, size_t len) { deliverToRouter(1, data, len); }
    void enqueueReceived(meshtastic_MeshPacket *p) override
    {
        received.push_back(*p);
        packetPool.release(p);
    }

  protected:
    bool platformReady() override { return true; }
    size_t platformPeers(BLEGattMeshPeer *, size_t) override { return 0; }
    bool platformNotify(BLEGattPeerId, const uint8_t *, size_t) override { return false; }
    bool platformPollInbound(BLEGattPeerId &, uint8_t *, size_t, size_t &) override { return false; }
};

bool deliverGatt(const meshtastic_MeshPacket &sent, meshtastic_MeshPacket &got)
{
    uint8_t body[meshtastic_MeshPacket_size];
    const size_t n = pb_encode_to_bytes(body, sizeof(body), &meshtastic_MeshPacket_msg, &sent);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "fixture packet encodes");
    GattIngress h;
    h.start();
    h.deliver(body, n);
    if (h.received.empty())
        return false;
    got = h.received[0];
    return true;
}
#endif

#if HAS_BLE_MESH
// The advertisement handler with its radio stubbed out, reached at the point a scanned payload arrives.
class AdvIngress : public BLEMeshHandler
{
  public:
    std::vector<meshtastic_MeshPacket> received;

    void start() override { isRunning = true; }
    void stop() override { isRunning = false; }
    void deliver(const uint8_t *data, size_t len) { deliverToRouter(data, len, kHopRssi); }
    void enqueueReceived(meshtastic_MeshPacket *p) override
    {
        received.push_back(*p);
        packetPool.release(p);
    }

  protected:
    bool platformBeginAdvertising(const uint8_t *, size_t) override { return false; }
    bool platformAdvertisingActive() override { return false; }
    void platformEndAdvertising() override {}
    bool platformReady() override { return true; }
};

bool deliverAdv(const meshtastic_MeshPacket &sent, meshtastic_MeshPacket &got)
{
    uint8_t body[meshtastic_MeshPacket_size];
    const size_t n = pb_encode_to_bytes(body, sizeof(body), &meshtastic_MeshPacket_msg, &sent);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "fixture packet encodes");
    AdvIngress h;
    h.start();
    h.deliver(body, n);
    if (h.received.empty())
        return false;
    got = h.received[0];
    return true;
}
#endif

const Bearer bearers[] = {
#if HAS_UDP_MULTICAST
    {"UDP", meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MULTICAST_UDP, deliverUdp, false},
#endif
    {"MQTT", meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT, deliverMqtt, false},
#if HAS_BLE_GATT_MESH
    {"BLE GATT", meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_GATT, deliverGatt, false},
#endif
#if HAS_BLE_MESH
    {"BLE advertisement", meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, deliverAdv, true},
#endif
};

meshtastic_MeshPacket encryptedPacket(NodeNum from = kSender)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = 0xffffffff;
    p.id = 0x0badf00d;
    p.channel = 0x5a;
    p.hop_limit = 3;
    p.hop_start = 3;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 16;
    memset(p.encrypted.bytes, 0xa5, p.encrypted.size);
    return p;
}

// Every local-only field set to a value a sender could choose.
meshtastic_MeshPacket withLocalOnlyFieldsSet(meshtastic_MeshPacket p)
{
    p.tx_after = 4242;
    p.priority = meshtastic_MeshPacket_Priority_MAX;
    p.pki_encrypted = true;
    p.public_key.size = 32;
    memset(p.public_key.bytes, 0x5c, sizeof(p.public_key.bytes));
    p.rx_snr = 9.5f;
    p.rx_rssi = -30;
    p.has_rx_rssi = true;
    p.rx_time = 1700000000;
    p.has_rx_time = true;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    return p;
}

// The LoRa-header fields a relay depends on, set to non-defaults.
meshtastic_MeshPacket withHeaderFieldsSet(meshtastic_MeshPacket p)
{
    p.hop_limit = 2;
    p.hop_start = 5;
    p.want_ack = true;
    p.via_mqtt = true;
    p.next_hop = 0x2e;
    p.relay_node = 0x7c;
    return p;
}

void assertLocalOnlyCleared(const meshtastic_MeshPacket &got, const char *name, bool measuresHop = false)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, got.tx_after, name);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_MeshPacket_Priority_UNSET, got.priority, name);
    TEST_ASSERT_FALSE_MESSAGE(got.pki_encrypted, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, got.public_key.size, name);
    // Router::send reads the key bytes without consulting the size, so the bytes are cleared too.
    uint8_t zeros[sizeof(got.public_key.bytes)] = {0};
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(zeros, got.public_key.bytes, sizeof(zeros), name);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0, got.rx_snr, name);
    // A sent value never survives: either this node's own measurement of the hop, or none at all.
    TEST_ASSERT_EQUAL_INT_MESSAGE(measuresHop ? kHopRssi : 0, got.rx_rssi, name);
    TEST_ASSERT_EQUAL_MESSAGE(measuresHop, got.has_rx_rssi, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, got.rx_time, name);
    TEST_ASSERT_FALSE_MESSAGE(got.has_rx_time, name);
}

void assertHeaderFieldsKept(const meshtastic_MeshPacket &got, const char *name)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, got.hop_limit, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, got.hop_start, name);
    TEST_ASSERT_TRUE_MESSAGE(got.want_ack, name);
    // ignore_mqtt and the MQTT uplink's loop guard read via_mqtt, and the LoRa header carries it.
    TEST_ASSERT_TRUE_MESSAGE(got.via_mqtt, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x2e, got.next_hop, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x7c, got.relay_node, name);
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_ingress_admits_and_stamps_the_bearer(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        TEST_ASSERT_TRUE_MESSAGE(b.deliver(encryptedPacket(), got), b.name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(kSender, got.from, b.name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x0badf00d, got.id, b.name);
        TEST_ASSERT_EQUAL_MESSAGE(b.medium, got.transport_mechanism, b.name);
        TEST_ASSERT_EQUAL_MESSAGE(16, got.encrypted.size, b.name);
        TEST_ASSERT_EQUAL_MESSAGE(b.measuresHop, got.has_rx_rssi, b.name);
    }
}

void test_ingress_drops_a_frame_with_no_sender(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        TEST_ASSERT_FALSE_MESSAGE(b.deliver(encryptedPacket(0), got), b.name);
    }
}

void test_ingress_drops_our_own_node_number(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        TEST_ASSERT_FALSE_MESSAGE(b.deliver(encryptedPacket(nodeDB->getNodeNum()), got), b.name);
    }
}

void test_ingress_drops_an_impossible_hop_count(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        auto limit = encryptedPacket();
        limit.hop_limit = HOP_MAX + 1;
        TEST_ASSERT_FALSE_MESSAGE(b.deliver(limit, got), b.name);

        auto start = encryptedPacket();
        start.hop_start = HOP_MAX + 1;
        TEST_ASSERT_FALSE_MESSAGE(b.deliver(start, got), b.name);

        auto edge = encryptedPacket();
        edge.hop_limit = HOP_MAX;
        edge.hop_start = HOP_MAX;
        TEST_ASSERT_TRUE_MESSAGE(b.deliver(edge, got), b.name);
    }
}

void test_ingress_clears_every_local_only_field(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        TEST_ASSERT_TRUE_MESSAGE(b.deliver(withLocalOnlyFieldsSet(encryptedPacket()), got), b.name);
        assertLocalOnlyCleared(got, b.name, b.measuresHop);
        TEST_ASSERT_EQUAL_MESSAGE(b.medium, got.transport_mechanism, b.name);
    }
}

void test_ingress_keeps_the_lora_header_fields(void)
{
    for (const auto &b : bearers) {
        meshtastic_MeshPacket got;
        TEST_ASSERT_TRUE_MESSAGE(b.deliver(withHeaderFieldsSet(encryptedPacket()), got), b.name);
        assertHeaderFieldsKept(got, b.name);
    }
}

void test_strip_removes_every_field_a_receiver_overwrites(void)
{
    const auto sent = withLocalOnlyFieldsSet(encryptedPacket());
    const auto air = MeshTransportBase::stripForTransmit(sent);
    assertLocalOnlyCleared(air, "strip");
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL, air.transport_mechanism);
    // The input is left alone: Router::send hands the same packet to every transport.
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_Priority_MAX, sent.priority);
}

void test_strip_keeps_the_lora_header_and_payload(void)
{
    const auto air = MeshTransportBase::stripForTransmit(withHeaderFieldsSet(encryptedPacket()));
    assertHeaderFieldsKept(air, "strip");
    TEST_ASSERT_EQUAL_UINT32(kSender, air.from);
    TEST_ASSERT_EQUAL_UINT32(0xffffffff, air.to);
    TEST_ASSERT_EQUAL_UINT32(0x0badf00d, air.id);
    TEST_ASSERT_EQUAL_UINT8(0x5a, air.channel);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, air.which_payload_variant);
    TEST_ASSERT_EQUAL(16, air.encrypted.size);
}

void setup()
{
    initializeTestEnvironment();
    // The own-node guard reads nodeDB->getNodeNum().
    if (!nodeDB)
        nodeDB = new NodeDB();

    UNITY_BEGIN();
    RUN_TEST(test_ingress_admits_and_stamps_the_bearer);
    RUN_TEST(test_ingress_drops_a_frame_with_no_sender);
    RUN_TEST(test_ingress_drops_our_own_node_number);
    RUN_TEST(test_ingress_drops_an_impossible_hop_count);
    RUN_TEST(test_ingress_clears_every_local_only_field);
    RUN_TEST(test_ingress_keeps_the_lora_header_fields);
    RUN_TEST(test_strip_removes_every_field_a_receiver_overwrites);
    RUN_TEST(test_strip_keeps_the_lora_header_and_payload);
    exit(UNITY_END());
}

void loop() {}
