#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO) && HAS_BLE_MESH

#include "mesh/BLEMeshHandler.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"

#include <vector>

namespace
{

/**
 * A BLEMeshHandler with the radio replaced by a record of what it was asked to send.
 *
 * Everything worth testing here is platform-independent - the advertisement the transport builds
 * and the guards it applies to what it receives - so the platform hooks only need to be observable,
 * not real.
 */
class FakeBLEMesh : public BLEMeshHandler
{
  public:
    std::vector<std::vector<uint8_t>> sent;
    std::vector<meshtastic_MeshPacket> received;
    bool ready = true;
    bool advertising = false;

    void start() override { isRunning = true; }
    void stop() override { isRunning = false; }

    // Exposed so tests can drive ingress without a BLE stack.
    void feed(const uint8_t *data, size_t len, int8_t rssi) { deliverToRouter(data, len, rssi); }
    uint8_t build(const meshtastic_MeshPacket *mp, uint8_t *out, size_t cap) { return buildAdvPayload(mp, out, cap); }
    int32_t pump() { return runOnce(); }

    void enqueueReceived(meshtastic_MeshPacket *p) override
    {
        received.push_back(*p);
        packetPool.release(p);
    }

  protected:
    bool platformBeginAdvertising(const uint8_t *adv, size_t len) override
    {
        sent.emplace_back(adv, adv + len);
        advertising = true;
        return true;
    }
    bool platformAdvertisingActive() override { return advertising; }
    void platformEndAdvertising() override { advertising = false; }
    bool platformReady() override { return ready; }
};

/// A packet in the state Router::send hands to a transport: encrypted, with a real sender.
meshtastic_MeshPacket encryptedPacket(uint32_t from = 0x3061b02e, uint32_t id = 0x04050b6e, size_t payload = 32)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = NODENUM_BROADCAST;
    p.id = id;
    p.channel = 50;
    p.hop_limit = 3;
    p.hop_start = 3;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = payload;
    for (size_t i = 0; i < payload; i++)
        p.encrypted.bytes[i] = (uint8_t)i;
    return p;
}

/// Largest ciphertext a MeshPacket can carry, i.e. one guaranteed not to fit an advertisement.
constexpr size_t MAX_ENCRYPTED_FOR_TEST = sizeof(meshtastic_MeshPacket().encrypted.bytes);

/// Encode `mp` the way the transport does, so ingress tests have a real advertisement body.
size_t encodeForAir(const meshtastic_MeshPacket &mp, uint8_t *out, size_t cap)
{
    return pb_encode_to_bytes(out, cap, &meshtastic_MeshPacket_msg, &mp);
}

} // namespace

void test_advertisement_carries_the_packet(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    uint8_t len = h.build(&p, adv, sizeof(adv));

    TEST_ASSERT_TRUE_MESSAGE(len > BLE_MESH_ADV_OVERHEAD, "built an advertisement");
    // Flags AD, then manufacturer-specific data with our company ID and protocol version.
    TEST_ASSERT_EQUAL_UINT8(2, adv[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, adv[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, adv[4]);
    TEST_ASSERT_EQUAL_UINT8(BLE_MESH_COMPANY_ID & 0xFF, adv[5]);
    TEST_ASSERT_EQUAL_UINT8((BLE_MESH_COMPANY_ID >> 8) & 0xFF, adv[6]);
    TEST_ASSERT_EQUAL_UINT8(BLE_MESH_PROTOCOL_VERSION, adv[7]);
    // The AD length byte counts everything after itself.
    TEST_ASSERT_EQUAL_UINT8(len - 4, adv[3]);
}

void test_refuses_an_unencrypted_packet(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    // Router::send encrypts before any transport sees a packet, so plaintext here is a bug
    // upstream - putting it on air would leak the message.
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, adv, sizeof(adv)));
}

void test_refuses_a_packet_with_no_sender(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0 /* from */);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, adv, sizeof(adv)));
}

void test_drops_a_packet_too_large_for_one_advertisement(void)
{
    FakeBLEMesh h;
    h.start();
    // A single unfragmented extended advertisement holds 251 bytes and we never chain, so the
    // largest packets cannot ride BLE. They still go out over LoRa - Router::send has already
    // handed them to the radio by the time we refuse.
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, MAX_ENCRYPTED_FOR_TEST);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, adv, sizeof(adv)));
}

void test_send_queues_rather_than_transmitting(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();

    TEST_ASSERT_TRUE(h.onSend(&p));
    // onSend is reached from Router::send on the main task. An implementation that advertised
    // inline would stall the router - and so LoRa timing and the whole main loop - for the length
    // of every burst.
    TEST_ASSERT_EQUAL_MESSAGE(0, h.sent.size(), "nothing on air yet");

    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.sent.size(), "the pump transmits it");
}

void test_tx_queue_is_bounded(void)
{
    FakeBLEMesh h;
    h.start();

    size_t accepted = 0;
    for (size_t i = 0; i < BLE_MESH_TX_QUEUE_SIZE * 3; i++) {
        auto p = encryptedPacket(0x3061b02e, (uint32_t)(0x1000 + i));
        if (h.onSend(&p))
            accepted++;
    }
    // A full ring drops rather than growing without bound or overwriting an unsent frame.
    TEST_ASSERT_EQUAL(BLE_MESH_TX_QUEUE_SIZE, accepted);
}

void test_a_relayed_packet_is_re_advertised(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV;

    // A packet already marked as BLE-sourced is what a *rebroadcast* looks like:
    // perhapsRebroadcast allocCopy()s the received packet and nothing on the TX path rewrites
    // transport_mechanism. Refusing it caps the mesh at a single hop - two nodes can talk and a
    // three-node chain cannot form.
    TEST_ASSERT_TRUE_MESSAGE(h.onSend(&p), "relay must not be refused");
}

void test_ingress_accepts_a_well_formed_frame(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();

    uint8_t body[meshtastic_MeshPacket_size];
    size_t n = encodeForAir(p, body, sizeof(body));
    TEST_ASSERT_TRUE(n > 0);

    h.feed(body, n, -42);

    TEST_ASSERT_EQUAL_MESSAGE(1, h.received.size(), "delivered to the router");
    const auto &got = h.received[0];
    TEST_ASSERT_EQUAL_UINT32(0x3061b02e, got.from);
    // Stamped so the router - and anything downstream - can tell how it arrived.
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, got.transport_mechanism);
    // Unlike UDP there IS a real measurement of this hop, so it is reported rather than cleared.
    TEST_ASSERT_TRUE(got.has_rx_rssi);
    TEST_ASSERT_EQUAL_INT(-42, got.rx_rssi);
    TEST_ASSERT_EQUAL_MESSAGE(0, got.rx_snr, "no SNR exists for a BLE arrival");
}

void test_ingress_drops_a_frame_with_no_sender(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0 /* from */);

    uint8_t body[meshtastic_MeshPacket_size];
    size_t n = encodeForAir(p, body, sizeof(body));

    // Nothing legitimate advertises from=0, and a packet with no sender can reach remote admin
    // without authorisation - the LoRa path refuses it for the same reason.
    h.feed(body, n, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "spoofed origin rejected");
}

void test_ingress_drops_an_impossible_hop_count(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();
    p.hop_limit = HOP_MAX + 1;

    uint8_t body[meshtastic_MeshPacket_size];
    size_t n = encodeForAir(p, body, sizeof(body));

    // An out-of-range hop count is not relayable; UdpMulticastHandler drops it identically.
    h.feed(body, n, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "invalid hop count rejected");
}

void test_ingress_clears_pki_metadata(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();
    // A sender must not be able to assert its own packet was PKI-authenticated: that flag is
    // local state the Router sets after a successful decrypt, never something off the wire.
    p.pki_encrypted = true;
    p.public_key.size = 32;

    uint8_t body[meshtastic_MeshPacket_size];
    size_t n = encodeForAir(p, body, sizeof(body));

    h.feed(body, n, -50);
    TEST_ASSERT_EQUAL(1, h.received.size());
    TEST_ASSERT_FALSE_MESSAGE(h.received[0].pki_encrypted, "claimed authentication stripped");
    TEST_ASSERT_EQUAL(0, h.received[0].public_key.size);
}

void test_ingress_ignores_our_own_advertisement(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(nodeDB->getNodeNum());

    uint8_t body[meshtastic_MeshPacket_size];
    size_t n = encodeForAir(p, body, sizeof(body));

    // Our own advertisement echoing back into our own scanner would loop.
    h.feed(body, n, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "self-echo dropped");
}

void test_pump_waits_for_the_platform(void)
{
    FakeBLEMesh h;
    h.start();
    h.ready = false;
    auto p = encryptedPacket();
    TEST_ASSERT_TRUE(h.onSend(&p));

    h.pump();
    // Readiness is polled rather than pushed: the BLE stack comes up before main() constructs the
    // handler about half the time, so a one-shot "ready" callback is a race that loses silently.
    TEST_ASSERT_EQUAL_MESSAGE(0, h.sent.size(), "nothing transmitted before the stack is up");

    h.ready = true;
    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.sent.size(), "transmits once ready");
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    // deliverToRouter consults nodeDB to recognise - and drop - our own advertisement echoing back
    // into our own scanner, so the ingress tests need a real one.
    if (!nodeDB)
        nodeDB = new NodeDB();
    UNITY_BEGIN();
    RUN_TEST(test_advertisement_carries_the_packet);
    RUN_TEST(test_refuses_an_unencrypted_packet);
    RUN_TEST(test_refuses_a_packet_with_no_sender);
    RUN_TEST(test_drops_a_packet_too_large_for_one_advertisement);
    RUN_TEST(test_send_queues_rather_than_transmitting);
    RUN_TEST(test_tx_queue_is_bounded);
    RUN_TEST(test_a_relayed_packet_is_re_advertised);
    RUN_TEST(test_ingress_accepts_a_well_formed_frame);
    RUN_TEST(test_ingress_drops_a_frame_with_no_sender);
    RUN_TEST(test_ingress_drops_an_impossible_hop_count);
    RUN_TEST(test_ingress_clears_pki_metadata);
    RUN_TEST(test_ingress_ignores_our_own_advertisement);
    RUN_TEST(test_pump_waits_for_the_platform);
    exit(UNITY_END());
}

#else

void setup()
{
    initializeTestEnvironment();
    LOG_WARN("BLE mesh tests require ARCH_PORTDUINO with HAS_BLE_MESH");
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif // ARCH_PORTDUINO && HAS_BLE_MESH

void loop() {}
