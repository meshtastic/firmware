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
    bool cancel(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id)
    {
        return onCancelSending(medium, from, id);
    }
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

/// What Router::send actually hands a transport, as opposed to the minimal fixture above.
///
/// fixPriority() runs before encryption and never leaves priority UNSET (Router.cpp:562), and
/// FloodingRouter::send stamps relay_node on everything we send (FloodingRouter.cpp:22). A relay
/// carries transport_mechanism and the reception metadata it was received with. Measuring the
/// ceiling against the minimal fixture measures the fixture, not the bearer.
meshtastic_MeshPacket productionPacket(size_t payload = 32, bool relayed = false)
{
    meshtastic_MeshPacket p = encryptedPacket(0x3061b02e, 0x04050b6e, payload);
    p.priority = meshtastic_MeshPacket_Priority_DEFAULT;
    p.relay_node = 0x2e;
    if (relayed) {
        p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
        p.rx_rssi = -113;
        p.has_rx_rssi = true;
        p.rx_snr = -7.25f;
        p.rx_time = 1757808000;
        p.has_rx_time = true;
    }
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

/// The largest ciphertext that still fits one advertisement, found rather than assumed.
///
/// The budget is BLE_MESH_MAX_PROTO_LEN for the *whole* encoded MeshPacket, so the answer is that
/// minus whatever envelope the packet happens to carry - which is why it is measured per packet
/// shape rather than written down once.
size_t largestCiphertextThatFits(meshtastic_MeshPacket shape)
{
    FakeBLEMesh h;
    h.start();
    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];

    size_t lo = 0, hi = MAX_ENCRYPTED_FOR_TEST;
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        shape.encrypted.size = mid;
        if (h.build(&shape, adv, sizeof(adv)) > 0)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void test_the_advertisement_ceiling_is_below_the_lora_ceiling(void)
{
    const size_t fits = largestCiphertextThatFits(productionPacket());

    // The bearer is not a full bearer and never has been: one unfragmented extended advertisement
    // cannot hold what one LoRa frame holds, and nothing fragments - the nRF52 SoftDevice caps both
    // the advertising data and the scan buffer at 255, so chaining is not available in either
    // direction. Everything above this rides LoRa only, counted by txDroppedTooLarge.
    // 216, not the 219 the minimal fixture reaches: relay_node costs 3 (field 19, so a two-byte
    // tag). priority costs nothing because strippedForAir drops it, which is also why node-kmp's
    // BleAdvertCeilingTest measures 214 for the same packet - it has no equivalent strip yet.
    TEST_ASSERT_EQUAL_size_t(216, fits);
    TEST_ASSERT_LESS_THAN_size_t_MESSAGE(MAX_RADIO_PAYLOAD_LEN, fits, "BLE carries less than LoRa");
}

void test_relaying_no_longer_costs_budget(void)
{
    // Before the egress strip a relay reached 21 bytes less far than the originator did, because
    // the packet went out carrying the rx_rssi, rx_snr and rx_time it arrived with. It now reaches
    // exactly as far: the receiver overwrites all three, so they were never worth sending.
    TEST_ASSERT_EQUAL_size_t(largestCiphertextThatFits(productionPacket()),
                             largestCiphertextThatFits(productionPacket(32, true)));
}

void test_the_air_copy_drops_everything_the_receiver_overwrites(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = productionPacket(32, true);
    p.via_mqtt = true;
    p.tx_after = 4242;
    p.pki_encrypted = true;
    p.public_key.size = 32;
    for (size_t i = 0; i < 32; i++)
        p.public_key.bytes[i] = (uint8_t)(0xA0 + i);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = h.build(&p, adv, sizeof(adv));
    TEST_ASSERT_TRUE(len > BLE_MESH_ADV_OVERHEAD);

    meshtastic_MeshPacket air = meshtastic_MeshPacket_init_zero;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, &meshtastic_MeshPacket_msg, &air));

    // Exactly the set deliverToRouter rewrites, plus rx_time which Router::handleReceived stamps.
    // A sender that omits them loses nothing and stops publishing its own link quality; one that
    // sends them is paying for bytes the far side discards.
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL, air.transport_mechanism);
    TEST_ASSERT_FALSE(air.via_mqtt);
    TEST_ASSERT_EQUAL_UINT32(0, air.tx_after);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_Priority_UNSET, air.priority);
    TEST_ASSERT_FALSE(air.pki_encrypted);
    TEST_ASSERT_EQUAL_UINT16(0, air.public_key.size);
    TEST_ASSERT_FALSE(air.has_rx_rssi);
    TEST_ASSERT_FALSE(air.has_rx_time);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, air.rx_snr);

    // And nothing routing needs went with them.
    TEST_ASSERT_EQUAL_UINT32(p.from, air.from);
    TEST_ASSERT_EQUAL_UINT32(p.to, air.to);
    TEST_ASSERT_EQUAL_UINT32(p.id, air.id);
    TEST_ASSERT_EQUAL_UINT8(p.hop_limit, air.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(p.hop_start, air.hop_start);
    TEST_ASSERT_EQUAL_UINT8(p.relay_node, air.relay_node);
    TEST_ASSERT_EQUAL_UINT32(p.channel, air.channel);
    TEST_ASSERT_EQUAL_UINT16(p.encrypted.size, air.encrypted.size);
}

void test_an_oversized_packet_is_counted_not_just_logged(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, MAX_ENCRYPTED_FOR_TEST);

    TEST_ASSERT_FALSE(h.onSend(&p));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, h.txDroppedTooLarge, "the loss is countable");
}

void test_a_dupe_heard_on_ble_cancels_our_queued_copy(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e);
    TEST_ASSERT_TRUE(h.onSend(&p));

    // A neighbour relayed it before we got to. One advertisement reaches every neighbour at once,
    // so their copy has already done our work.
    TEST_ASSERT_TRUE(h.cancel(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, p.from, p.id));

    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(0, h.sent.size(), "nothing went out");
}

void test_a_dupe_heard_on_lora_leaves_the_ble_queue_alone(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e);
    TEST_ASSERT_TRUE(h.onSend(&p));

    // Hearing a LoRa neighbour relay this says nothing about whether our BLE neighbours have it.
    // Cancelling here would silently thin the BLE flood every time the two meshes overlap.
    TEST_ASSERT_FALSE(h.cancel(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA, p.from, p.id));

    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.sent.size(), "still advertised");
}

void test_canceling_keeps_the_other_queued_frames_in_order(void)
{
    FakeBLEMesh h;
    h.start();
    auto first = encryptedPacket(0x3061b02e, 0x11111111);
    auto doomed = encryptedPacket(0x3061b02e, 0x22222222);
    auto last = encryptedPacket(0x3061b02e, 0x33333333);
    TEST_ASSERT_TRUE(h.onSend(&first));
    TEST_ASSERT_TRUE(h.onSend(&doomed));
    TEST_ASSERT_TRUE(h.onSend(&last));

    TEST_ASSERT_TRUE(h.cancel(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, doomed.from, doomed.id));

    // Compacting the ring must not drop or reorder its neighbours - the frames either side are
    // unrelated packets that still have to go out, in the order they were queued.
    uint8_t expectedFirst[BLE_MESH_ADV_TOTAL_MAX];
    uint8_t expectedLast[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t firstLen = h.build(&first, expectedFirst, sizeof(expectedFirst));
    const uint8_t lastLen = h.build(&last, expectedLast, sizeof(expectedLast));

    h.pump();
    h.advertising = false;
    h.pump();
    h.advertising = false;
    h.pump();

    TEST_ASSERT_EQUAL_MESSAGE(2, h.sent.size(), "two frames survived");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFirst, h.sent[0].data(), firstLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedLast, h.sent[1].data(), lastLen);
}

void test_canceling_cuts_a_burst_already_on_air(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e);
    TEST_ASSERT_TRUE(h.onSend(&p));
    h.pump();
    TEST_ASSERT_TRUE_MESSAGE(h.advertising, "on air");

    // The payload repeats for BLE_MESH_ADV_EVENTS events, so the copies still to come are exactly
    // what the overhear says are unnecessary. runOnce pops before it advertises, so this frame is
    // no longer in the ring and only the live-burst identity can find it.
    TEST_ASSERT_TRUE(h.cancel(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, p.from, p.id));
    TEST_ASSERT_FALSE_MESSAGE(h.advertising, "burst ended");

    // And the state machine is not left half-advanced: the next pump finds an empty ring and idles
    // rather than re-ending a burst that is already over.
    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.sent.size(), "nothing re-sent");
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
    RUN_TEST(test_the_advertisement_ceiling_is_below_the_lora_ceiling);
    RUN_TEST(test_relaying_no_longer_costs_budget);
    RUN_TEST(test_the_air_copy_drops_everything_the_receiver_overwrites);
    RUN_TEST(test_an_oversized_packet_is_counted_not_just_logged);
    RUN_TEST(test_a_dupe_heard_on_ble_cancels_our_queued_copy);
    RUN_TEST(test_a_dupe_heard_on_lora_leaves_the_ble_queue_alone);
    RUN_TEST(test_canceling_keeps_the_other_queued_frames_in_order);
    RUN_TEST(test_canceling_cuts_a_burst_already_on_air);
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
