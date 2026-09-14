#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "TestUtil.h"
#include "concurrency/LockGuard.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO) && HAS_BLE_MESH

#if HAS_SCREEN
#include "graphics/draw/MenuHandler.h"
#endif
#include "mesh/BLEMeshHandler.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/PacketHistory.h"
#include "mesh/Router.h"

#include <Curve25519.h>
#include <ErriezCRC32.h>
#include <array>
#include <vector>

namespace
{

/**
 * A BLEMeshHandler with the radio replaced by a record of what it was asked to send.
 *
 * Everything worth testing here is platform-independent - the advertisement the transport builds
 * and the guards it applies to what it receives - so the platform hooks only need to be observable,
 * not real. The secure envelope, approval checks, verification code, and tamper detection are also
 * pinned here.
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
    uint8_t build(const meshtastic_MeshPacket *mp, NodeNum peer, uint8_t *out, size_t cap)
    {
        return buildAdvPayload(mp, peer, out, cap);
    }
    bool approveForTest(NodeNum nodeNum, const uint8_t publicKey[32]) { return addPairForTest(nodeNum, publicKey); }
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

struct TestIdentity {
    std::array<uint8_t, 32> privateKey{};
    std::array<uint8_t, 32> publicKey{};
    NodeNum nodeNum = 0;
};

TestIdentity makeIdentity(uint8_t seed)
{
    TestIdentity identity;
    for (size_t i = 0; i < identity.privateKey.size(); i++)
        identity.privateKey[i] = seed + static_cast<uint8_t>(i);
    Curve25519::eval(identity.publicKey.data(), identity.privateKey.data(), nullptr);
    identity.nodeNum = crc32Buffer(identity.publicKey.data(), identity.publicKey.size());
    return identity;
}

void useIdentity(const TestIdentity &identity)
{
    config.security.private_key.size = 32;
    config.security.public_key.size = 32;
    memcpy(config.security.private_key.bytes, identity.privateKey.data(), 32);
    memcpy(config.security.public_key.bytes, identity.publicKey.data(), 32);
    owner.public_key.size = 32;
    memcpy(owner.public_key.bytes, identity.publicKey.data(), 32);
    myNodeInfo.my_node_num = identity.nodeNum;
    crypto->setDHPrivateKey(config.security.private_key.bytes);
}

std::vector<uint8_t> frameBody(const std::vector<uint8_t> &advertisement)
{
    return std::vector<uint8_t>(advertisement.begin() + BLE_MESH_ADV_OVERHEAD, advertisement.end());
}

/// Largest ciphertext a MeshPacket can carry, i.e. one guaranteed not to fit an advertisement.
constexpr size_t MAX_ENCRYPTED_FOR_TEST = sizeof(meshtastic_MeshPacket().encrypted.bytes);

uint8_t buildAuthenticatedAdvertisement(FakeBLEMesh &handler, const TestIdentity &sender, const TestIdentity &recipient,
                                        const meshtastic_MeshPacket &packet, uint8_t out[BLE_MESH_ADV_TOTAL_MAX])
{
    useIdentity(sender);
    handler.start();
    if (!handler.approveForTest(recipient.nodeNum, recipient.publicKey.data()))
        return 0;
    return handler.build(&packet, recipient.nodeNum, out, BLE_MESH_ADV_TOTAL_MAX);
}

uint32_t readTestU32(const uint8_t *in)
{
    uint32_t value;
    memcpy(&value, in, sizeof(value));
    return value;
}

} // namespace

void test_advertisement_carries_the_packet(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    auto p = encryptedPacket();

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    uint8_t len = buildAuthenticatedAdvertisement(h, sender, recipient, p, adv);

    TEST_ASSERT_TRUE_MESSAGE(len > BLE_MESH_ADV_OVERHEAD, "built an advertisement");
    // Flags AD, then manufacturer-specific data with our company ID and protocol version.
    TEST_ASSERT_EQUAL_UINT8(2, adv[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, adv[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, adv[4]);
    TEST_ASSERT_EQUAL_UINT8(BLE_MESH_COMPANY_ID & 0xFF, adv[5]);
    TEST_ASSERT_EQUAL_UINT8((BLE_MESH_COMPANY_ID >> 8) & 0xFF, adv[6]);
    TEST_ASSERT_EQUAL_UINT8(BLE_MESH_PROTOCOL_VERSION, adv[7]);
    TEST_ASSERT_EQUAL_UINT8(BLE_MESH_FRAME_DATA, adv[BLE_MESH_ADV_OVERHEAD]);
    TEST_ASSERT_EQUAL_UINT32(sender.nodeNum, readTestU32(&adv[BLE_MESH_ADV_OVERHEAD + 1]));
    TEST_ASSERT_EQUAL_UINT32(recipient.nodeNum, readTestU32(&adv[BLE_MESH_ADV_OVERHEAD + 5]));
    // The AD length byte counts everything after itself.
    TEST_ASSERT_EQUAL_UINT8(len - 4, adv[3]);
}

void test_refuses_an_unencrypted_packet(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    auto p = encryptedPacket();
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    // Router::send encrypts before any transport sees a packet, so plaintext here is a bug
    // upstream - putting it on air would leak the message.
    h.approveForTest(recipient.nodeNum, recipient.publicKey.data());
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, recipient.nodeNum, adv, sizeof(adv)));
}

void test_refuses_a_packet_with_no_sender(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    auto p = encryptedPacket(0 /* from */);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    // Nothing legitimate advertises from=0, and a packet with no sender can reach remote admin
    // without authorisation - the LoRa path refuses it for the same reason.
    h.approveForTest(recipient.nodeNum, recipient.publicKey.data());
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, recipient.nodeNum, adv, sizeof(adv)));
}

void test_drops_a_packet_too_large_for_one_advertisement(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    // A single unfragmented extended advertisement holds 251 bytes and we never chain, so the
    // largest packets cannot ride BLE. They still go out over LoRa - Router::send has already
    // handed them to the radio by the time we refuse.
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, MAX_ENCRYPTED_FOR_TEST);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    h.approveForTest(recipient.nodeNum, recipient.publicKey.data());
    TEST_ASSERT_EQUAL_UINT8(0, h.build(&p, recipient.nodeNum, adv, sizeof(adv)));
}

void test_send_queues_rather_than_transmitting(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    TEST_ASSERT_TRUE(h.approveForTest(recipient.nodeNum, recipient.publicKey.data()));
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
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    TEST_ASSERT_TRUE(h.approveForTest(recipient.nodeNum, recipient.publicKey.data()));

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
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    TEST_ASSERT_TRUE(h.approveForTest(recipient.nodeNum, recipient.publicKey.data()));
    auto p = encryptedPacket();
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV;

    // A packet already marked as BLE-sourced is what a *rebroadcast* looks like:
    // perhapsRebroadcast allocCopy()s the received packet and nothing on the TX path rewrites
    // transport_mechanism. Refusing it caps the mesh at a single hop - two nodes can talk and a
    // three-node chain cannot form.
    TEST_ASSERT_TRUE_MESSAGE(h.onSend(&p), "relay must not be refused");
}

void test_pairing_hello_produces_the_same_verification_code(void)
{
    const auto a = makeIdentity(1);
    const auto b = makeIdentity(65);
    FakeBLEMesh nodeA;
    FakeBLEMesh nodeB;
    uint32_t codeA = UINT32_MAX;
    uint32_t codeB = UINT32_MAX;

    useIdentity(a);
    nodeA.start();
    TEST_ASSERT_TRUE(nodeA.beginPairing([&codeA](NodeNum, uint32_t code) { codeA = code; }));
    nodeA.pump();
    TEST_ASSERT_EQUAL(1, nodeA.sent.size());
    nodeA.advertising = false;

    useIdentity(b);
    nodeB.start();
    TEST_ASSERT_TRUE(nodeB.beginPairing([&codeB](NodeNum, uint32_t code) { codeB = code; }));
    nodeB.pump();
    TEST_ASSERT_EQUAL(1, nodeB.sent.size());
    nodeB.advertising = false;

    auto helloA = frameBody(nodeA.sent[0]);
    auto helloB = frameBody(nodeB.sent[0]);
    nodeB.feed(helloA.data(), helloA.size(), -40);
    nodeB.pump();
    useIdentity(a);
    nodeA.feed(helloB.data(), helloB.size(), -41);
    nodeA.pump();

    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, codeA);
    TEST_ASSERT_EQUAL(codeA, codeB);
    nodeA.cancelPairing();
    nodeB.cancelPairing();
}

void test_approved_peer_survives_a_handler_restart(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);

    FakeBLEMesh first;
    TEST_ASSERT_TRUE(first.approveForTest(recipient.nodeNum, recipient.publicKey.data()));

    FakeBLEMesh restarted;
    TEST_ASSERT_TRUE(restarted.isPaired(recipient.nodeNum));
    TEST_ASSERT_EQUAL_UINT8(1, restarted.pairedCount());
}

void test_ingress_accepts_an_authenticated_approved_peer(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    FakeBLEMesh outbound;
    FakeBLEMesh inbound;
    auto p = encryptedPacket(0x3061b02e);

    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = buildAuthenticatedAdvertisement(outbound, sender, recipient, p, adv);
    TEST_ASSERT_TRUE(len > BLE_MESH_ADV_OVERHEAD);

    useIdentity(recipient);
    inbound.start();
    TEST_ASSERT_TRUE(inbound.approveForTest(sender.nodeNum, sender.publicKey.data()));
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -42);

    TEST_ASSERT_EQUAL_MESSAGE(1, inbound.received.size(), "delivered to the router");
    const auto &got = inbound.received[0];
    TEST_ASSERT_EQUAL_UINT32(0x3061b02e, got.from);
    // Stamped so the router - and anything downstream - can tell how it arrived.
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV, got.transport_mechanism);
    // Unlike UDP there IS a real measurement of this hop, so it is reported rather than cleared.
    TEST_ASSERT_TRUE(got.has_rx_rssi);
    TEST_ASSERT_EQUAL_INT(-42, got.rx_rssi);
    TEST_ASSERT_EQUAL_MESSAGE(0, got.rx_snr, "no SNR exists for a BLE arrival");
    TEST_ASSERT_FALSE_MESSAGE(got.pki_encrypted, "claimed authentication stripped");
    TEST_ASSERT_EQUAL(0, got.public_key.size);
}

void test_ingress_rejects_an_unapproved_peer(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    FakeBLEMesh outbound;
    FakeBLEMesh inbound;
    auto p = encryptedPacket();
    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = buildAuthenticatedAdvertisement(outbound, sender, recipient, p, adv);

    useIdentity(recipient);
    inbound.start();
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, inbound.received.size(), "unapproved bridge rejected");
}

void test_ingress_rejects_a_tampered_frame(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    FakeBLEMesh outbound;
    FakeBLEMesh inbound;
    auto p = encryptedPacket();
    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = buildAuthenticatedAdvertisement(outbound, sender, recipient, p, adv);
    adv[len - 1] ^= 0x80;

    useIdentity(recipient);
    inbound.start();
    TEST_ASSERT_TRUE(inbound.approveForTest(sender.nodeNum, sender.publicKey.data()));
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, inbound.received.size(), "tampered frame rejected");
}

void test_existing_packet_history_rejects_an_authenticated_replay(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    FakeBLEMesh outbound;
    FakeBLEMesh inbound;
    auto p = encryptedPacket();
    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = buildAuthenticatedAdvertisement(outbound, sender, recipient, p, adv);

    useIdentity(recipient);
    inbound.start();
    TEST_ASSERT_TRUE(inbound.approveForTest(sender.nodeNum, sender.publicKey.data()));
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -50);
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -50);
    TEST_ASSERT_EQUAL_MESSAGE(2, inbound.received.size(), "transport hands authenticated packets to the router");
    PacketHistory history(4);
    TEST_ASSERT_FALSE(history.wasSeenRecently(&inbound.received[0]));
    TEST_ASSERT_TRUE_MESSAGE(history.wasSeenRecently(&inbound.received[1]), "existing packet history rejects replay");
}

void test_ingress_rejects_the_removed_legacy_format(void)
{
    FakeBLEMesh h;
    h.start();
    auto p = encryptedPacket();
    uint8_t body[meshtastic_MeshPacket_size];
    const size_t n = pb_encode_to_bytes(body, sizeof(body), &meshtastic_MeshPacket_msg, &p);

    h.feed(body, n, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "legacy unauthenticated frame rejected");
}

void test_ingress_drops_an_impossible_hop_count(void)
{
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    FakeBLEMesh outbound;
    FakeBLEMesh inbound;
    auto p = encryptedPacket();
    p.hop_limit = HOP_MAX + 1;
    uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
    const uint8_t len = buildAuthenticatedAdvertisement(outbound, sender, recipient, p, adv);

    useIdentity(recipient);
    inbound.start();
    TEST_ASSERT_TRUE(inbound.approveForTest(sender.nodeNum, sender.publicKey.data()));
    inbound.feed(adv + BLE_MESH_ADV_OVERHEAD, len - BLE_MESH_ADV_OVERHEAD, -50);
    TEST_ASSERT_EQUAL_MESSAGE(0, inbound.received.size(), "invalid hop count rejected");
}

void test_pump_waits_for_the_platform(void)
{
    FakeBLEMesh h;
    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    h.start();
    TEST_ASSERT_TRUE(h.approveForTest(recipient.nodeNum, recipient.publicKey.data()));
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

#if HAS_BLE_MESH
// menuHandler::setNodePairingEnabled() in MenuHandler.cpp is the System-screen control plane for
// this transport. This pins the flag preservation and lifecycle calls so the menu cannot become
// cosmetic, or silently disable another configured network transport while enabling BLE sharing.
void test_node_pairing_menu_controls_ble_mesh_transport()
{
    struct StateRestore {
        meshtastic_LocalConfig savedConfig;
        BLEMeshHandler *savedHandler;
        ~StateRestore()
        {
            bleMeshHandler = savedHandler;
            config = savedConfig;
        }
    } restore{config, bleMeshHandler};

    FakeBLEMesh handler;
    bleMeshHandler = &handler;

    config.has_network = false;
    config.bluetooth.enabled = false;
    config.network.enabled_protocols =
        meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST | meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER;

    TEST_ASSERT_TRUE(graphics::menuHandler::setNodePairingEnabled(true));
    TEST_ASSERT_TRUE(config.has_network);
    TEST_ASSERT_TRUE(config.bluetooth.enabled);
    TEST_ASSERT_BITS_HIGH(meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_BROADCAST, config.network.enabled_protocols);
    TEST_ASSERT_BITS_HIGH(meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST |
                              meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER,
                          config.network.enabled_protocols);

    const auto sender = makeIdentity(1);
    const auto recipient = makeIdentity(65);
    useIdentity(sender);
    TEST_ASSERT_TRUE(handler.approveForTest(recipient.nodeNum, recipient.publicKey.data()));
    auto packet = encryptedPacket();
    TEST_ASSERT_TRUE(handler.onSend(&packet));

    TEST_ASSERT_FALSE(graphics::menuHandler::setNodePairingEnabled(false));
    TEST_ASSERT_BITS_LOW(meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_BROADCAST, config.network.enabled_protocols);
    TEST_ASSERT_BITS_HIGH(meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST |
                              meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER,
                          config.network.enabled_protocols);
    TEST_ASSERT_FALSE(handler.onSend(&packet));
}
#endif

void setUp(void) {}
void tearDown(void)
{
    concurrency::LockGuard guard(spiLock);
    if (FSCom.exists("/prefs/ble-pairs.dat"))
        FSCom.remove("/prefs/ble-pairs.dat");
}

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
    RUN_TEST(test_pairing_hello_produces_the_same_verification_code);
    RUN_TEST(test_approved_peer_survives_a_handler_restart);
    RUN_TEST(test_ingress_accepts_an_authenticated_approved_peer);
    RUN_TEST(test_ingress_rejects_an_unapproved_peer);
    RUN_TEST(test_ingress_rejects_a_tampered_frame);
    RUN_TEST(test_existing_packet_history_rejects_an_authenticated_replay);
    RUN_TEST(test_ingress_rejects_the_removed_legacy_format);
    RUN_TEST(test_ingress_drops_an_impossible_hop_count);
    RUN_TEST(test_pump_waits_for_the_platform);
#if HAS_BLE_MESH
    RUN_TEST(test_node_pairing_menu_controls_ble_mesh_transport);
#endif
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
