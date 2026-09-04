#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO) && HAS_BLE_GATT_MESH

#include "mesh/BLEGattMeshHandler.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"

#include <deque>
#include <map>
#include <vector>

namespace
{

/**
 * A BLEGattMeshHandler with the BLE stack replaced by a record of what it was asked to do.
 *
 * Everything worth testing is platform-independent - the fragment framing, the reassembly bounds, the
 * ingress guards, and which peer each fragment goes to - so the platform hooks only need to be
 * observable, not real.
 */
class FakeGattMesh : public BLEGattMeshHandler
{
  public:
    std::vector<BLEGattMeshPeer> peers;
    std::map<BLEGattPeerId, std::vector<std::vector<uint8_t>>> notified;
    std::deque<std::pair<BLEGattPeerId, std::vector<uint8_t>>> inbound;
    std::vector<meshtastic_MeshPacket> received;
    bool ready = true;
    bool busy = false;
    int notifyCalls = 0;

    void start() override { isRunning = true; }
    void stop() override { isRunning = false; }

    void feed(BLEGattPeerId peer, const std::vector<uint8_t> &chunk, uint32_t nowMs = 0)
    {
        handleChunk(peer, chunk.data(), chunk.size(), nowMs);
    }
    void lost(BLEGattPeerId peer, uint32_t nowMs = 0) { handleChunk(peer, nullptr, 0, nowMs); }
    int32_t pump() { return runOnce(); }
    size_t pending() const { return pendingAssemblies(); }

    void enqueueReceived(meshtastic_MeshPacket *p) override
    {
        received.push_back(*p);
        packetPool.release(p);
    }

  protected:
    bool platformReady() override { return ready; }
    size_t platformPeers(BLEGattMeshPeer *out, size_t cap) override
    {
        const size_t n = std::min(cap, peers.size());
        for (size_t i = 0; i < n; i++)
            out[i] = peers[i];
        return n;
    }
    bool platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len) override
    {
        notifyCalls++;
        if (busy)
            return false;
        notified[peer].emplace_back(data, data + len);
        return true;
    }
    bool platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len) override
    {
        if (inbound.empty())
            return false;
        const auto &front = inbound.front();
        peer = front.first;
        len = std::min(cap, front.second.size());
        memcpy(buf, front.second.data(), len);
        inbound.pop_front();
        return true;
    }
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
        p.encrypted.bytes[i] = (uint8_t)(i * 7 + 1);
    return p;
}

constexpr size_t MAX_ENCRYPTED_FOR_TEST = sizeof(meshtastic_MeshPacket().encrypted.bytes);

std::vector<uint8_t> encode(const meshtastic_MeshPacket &mp)
{
    std::vector<uint8_t> out(meshtastic_MeshPacket_size);
    const size_t n = pb_encode_to_bytes(out.data(), out.size(), &meshtastic_MeshPacket_msg, &mp);
    out.resize(n);
    return out;
}

/// Split a packet the way a client does: one fragment per write at this chunk size.
std::vector<std::vector<uint8_t>> split(const std::vector<uint8_t> &packet, uint16_t fragId, uint16_t chunk)
{
    std::vector<std::vector<uint8_t>> out;
    const uint8_t total = BLEGattMeshHandler::fragmentCount(packet.size(), chunk);
    for (uint8_t i = 0; i < total; i++) {
        std::vector<uint8_t> frag(chunk);
        const size_t n =
            BLEGattMeshHandler::buildFragment(packet.data(), packet.size(), fragId, i, total, chunk, frag.data(), frag.size());
        frag.resize(n);
        out.push_back(frag);
    }
    return out;
}

std::vector<uint8_t> reassembleAll(const std::vector<std::vector<uint8_t>> &frags)
{
    std::vector<uint8_t> out;
    for (const auto &f : frags)
        out.insert(out.end(), f.begin() + BLE_GATT_MESH_FRAG_HEADER, f.end());
    return out;
}

} // namespace

// --- framing ------------------------------------------------------------------------------------

void test_fragment_header_matches_the_client_format(void)
{
    std::vector<uint8_t> packet(40, 0xAB);
    auto frags = split(packet, 0xBEEF, 25);

    // 25-byte chunks carry 20 bytes of payload, so 40 bytes is exactly two fragments.
    TEST_ASSERT_EQUAL(2, frags.size());
    TEST_ASSERT_EQUAL(25, frags[0].size());
    TEST_ASSERT_EQUAL_UINT8(BLE_GATT_MESH_FRAG_VERSION, frags[0][0]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, frags[0][1]); // id, little-endian
    TEST_ASSERT_EQUAL_UINT8(0xBE, frags[0][2]);
    TEST_ASSERT_EQUAL_UINT8(0, frags[0][3]); // index
    TEST_ASSERT_EQUAL_UINT8(2, frags[0][4]); // total
    TEST_ASSERT_EQUAL_UINT8(1, frags[1][3]);
    TEST_ASSERT_EQUAL_UINT8(2, frags[1][4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(packet.data(), reassembleAll(frags).data(), packet.size());

    BLEGattMeshHandler::FragmentHeader hdr;
    TEST_ASSERT_TRUE(BLEGattMeshHandler::parseFragment(frags[1].data(), frags[1].size(), hdr));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, hdr.id);
    TEST_ASSERT_EQUAL_UINT8(1, hdr.index);
    TEST_ASSERT_EQUAL_UINT8(2, hdr.total);
}

void test_fragment_count_bounds(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, BLEGattMeshHandler::fragmentCount(10, BLE_GATT_MESH_FRAG_HEADER)); // no room for payload
    TEST_ASSERT_EQUAL_UINT8(1, BLEGattMeshHandler::fragmentCount(0, 20));                         // empty is still one
    TEST_ASSERT_EQUAL_UINT8(30, BLEGattMeshHandler::fragmentCount(450, 20));                      // 15 bytes each
    TEST_ASSERT_EQUAL_UINT8(1, BLEGattMeshHandler::fragmentCount(450, 512));
    // total is one byte, so a packet needing more than 255 pieces cannot be carried at all.
    TEST_ASSERT_EQUAL_UINT8(0, BLEGattMeshHandler::fragmentCount(255 * 15 + 1, 20));

    uint8_t out[32];
    // An index past the end, or a chunk with no payload room, is refused rather than truncated.
    TEST_ASSERT_EQUAL(0, BLEGattMeshHandler::buildFragment(out, 10, 1, 1, 1, 20, out, sizeof(out)));
    TEST_ASSERT_EQUAL(0, BLEGattMeshHandler::buildFragment(out, 10, 1, 0, 1, 5, out, sizeof(out)));
}

void test_parse_rejects_foreign_or_contradictory_headers(void)
{
    BLEGattMeshHandler::FragmentHeader hdr;
    const uint8_t wrongVersion[] = {2, 0, 0, 0, 1};
    const uint8_t zeroTotal[] = {1, 0, 0, 0, 0};
    const uint8_t indexPastTotal[] = {1, 0, 0, 3, 3};
    const uint8_t tooShort[] = {1, 0, 0, 0};
    TEST_ASSERT_FALSE(BLEGattMeshHandler::parseFragment(wrongVersion, sizeof(wrongVersion), hdr));
    TEST_ASSERT_FALSE(BLEGattMeshHandler::parseFragment(zeroTotal, sizeof(zeroTotal), hdr));
    TEST_ASSERT_FALSE(BLEGattMeshHandler::parseFragment(indexPastTotal, sizeof(indexPastTotal), hdr));
    TEST_ASSERT_FALSE(BLEGattMeshHandler::parseFragment(tooShort, sizeof(tooShort), hdr));
}

// --- reassembly ---------------------------------------------------------------------------------

void test_reassembles_a_fragmented_packet(void)
{
    FakeGattMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, 200);
    auto frags = split(encode(p), 7, 30);
    TEST_ASSERT_TRUE(frags.size() > 5);

    for (size_t i = 0; i < frags.size(); i++) {
        h.feed(1, frags[i]);
        TEST_ASSERT_EQUAL_MESSAGE(i + 1 == frags.size() ? 1 : 0, h.received.size(), "delivered only once complete");
    }
    TEST_ASSERT_EQUAL_UINT32(0x3061b02e, h.received[0].from);
    TEST_ASSERT_EQUAL_UINT32(0x04050b6e, h.received[0].id);
    TEST_ASSERT_EQUAL(200, h.received[0].encrypted.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p.encrypted.bytes, h.received[0].encrypted.bytes, 200);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_GATT, h.received[0].transport_mechanism);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.pending(), "the slot is released on completion");
}

void test_a_full_size_packet_survives_the_smallest_chunk(void)
{
    FakeGattMesh h;
    h.start();
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, MAX_ENCRYPTED_FOR_TEST);
    auto frags = split(encode(p), 1, BLE_GATT_MESH_MIN_CHUNK);
    for (const auto &f : frags)
        h.feed(1, f);
    TEST_ASSERT_EQUAL(1, h.received.size());
    TEST_ASSERT_EQUAL(MAX_ENCRYPTED_FOR_TEST, h.received[0].encrypted.size);
}

void test_single_fragment_completes_without_a_slot(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket()), 3, 512);
    TEST_ASSERT_EQUAL(1, frags.size());
    h.feed(1, frags[0]);
    TEST_ASSERT_EQUAL(1, h.received.size());
    TEST_ASSERT_EQUAL(0, h.pending());
}

void test_a_gap_drops_the_assembly(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 1, 100)), 9, 30);
    TEST_ASSERT_TRUE(frags.size() >= 3);

    h.feed(1, frags[0]);
    TEST_ASSERT_EQUAL(1, h.pending());
    // ATT delivers one peer's writes in order, so a missing index means the packet is lost, not late.
    h.feed(1, frags[2]);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.pending(), "a gap frees the slot rather than waiting for expiry");
    h.feed(1, frags[1]);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.pending(), "a packet cannot start mid-way");
    TEST_ASSERT_EQUAL(0, h.received.size());
}

void test_a_repeated_fragment_is_a_no_op(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 2, 40)), 9, 30);
    TEST_ASSERT_TRUE(frags.size() >= 2);
    h.feed(1, frags[0]);
    h.feed(1, frags[0]); // a retrying link re-delivers
    for (size_t i = 1; i < frags.size(); i++)
        h.feed(1, frags[i]);
    TEST_ASSERT_EQUAL(1, h.received.size());
}

void test_in_flight_packets_per_peer_are_bounded(void)
{
    FakeGattMesh h;
    h.start();
    for (uint16_t id = 1; id <= BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER + 2; id++) {
        auto frags = split(encode(encryptedPacket(0x3061b02e, id, 40)), id, 30);
        h.feed(1, frags[0]); // start, never finish
    }
    TEST_ASSERT_EQUAL(BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER, h.pending());
}

void test_peers_holding_slots_are_bounded(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 5, 40)), 5, 30);
    for (BLEGattPeerId peer = 1; peer <= BLE_GATT_MESH_MAX_PEERS + 2; peer++)
        h.feed(peer, frags[0]);
    TEST_ASSERT_EQUAL(BLE_GATT_MESH_MAX_PEERS, h.pending());
}

void test_half_built_packets_expire(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 6, 40)), 6, 30);
    h.feed(1, frags[0], 1000);
    TEST_ASSERT_EQUAL(1, h.pending());
    // The second half arrives after the expiry: the first is gone, and a packet cannot start at index 1.
    h.feed(1, frags[1], 1000 + BLE_GATT_MESH_REASSEMBLY_EXPIRY_MS);
    TEST_ASSERT_EQUAL(0, h.pending());
    TEST_ASSERT_EQUAL(0, h.received.size());
}

void test_a_lost_peer_forgets_its_assemblies(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 8, 40)), 8, 30);
    h.feed(1, frags[0]);
    h.feed(2, frags[0]);
    TEST_ASSERT_EQUAL(2, h.pending());
    h.lost(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, h.pending(), "only the departed peer's slot is freed");
}

// --- ingress guards -----------------------------------------------------------------------------

void test_ingress_drops_a_packet_with_no_sender(void)
{
    FakeGattMesh h;
    h.start();
    h.feed(1, split(encode(encryptedPacket(0 /* from */)), 1, 512)[0]);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "spoofed origin rejected");
}

void test_ingress_drops_a_packet_claiming_to_be_us(void)
{
    FakeGattMesh h;
    h.start();
    h.feed(1, split(encode(encryptedPacket(nodeDB->getNodeNum())), 1, 512)[0]);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "a peer cannot speak as this node");
}

void test_ingress_drops_an_impossible_hop_count(void)
{
    FakeGattMesh h;
    h.start();
    auto p = encryptedPacket();
    p.hop_limit = HOP_MAX + 1;
    h.feed(1, split(encode(p), 1, 512)[0]);
    TEST_ASSERT_EQUAL(0, h.received.size());
}

void test_ingress_clears_local_only_metadata(void)
{
    FakeGattMesh h;
    h.start();
    auto p = encryptedPacket();
    p.pki_encrypted = true;
    p.public_key.size = 32;
    p.rx_rssi = -40;
    p.has_rx_rssi = true;
    p.rx_snr = 9;
    h.feed(1, split(encode(p), 1, 512)[0]);
    TEST_ASSERT_EQUAL(1, h.received.size());
    TEST_ASSERT_FALSE_MESSAGE(h.received[0].pki_encrypted, "claimed authentication stripped");
    TEST_ASSERT_EQUAL(0, h.received[0].public_key.size);
    TEST_ASSERT_FALSE_MESSAGE(h.received[0].has_rx_rssi, "no radio measurement exists for a GATT arrival");
    TEST_ASSERT_EQUAL(0, h.received[0].rx_snr);
}

void test_ingress_rejects_bytes_that_do_not_decode(void)
{
    FakeGattMesh h;
    h.start();
    std::vector<uint8_t> garbage(60, 0xFF);
    h.feed(1, split(garbage, 1, 512)[0]);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.received.size(), "validate before relay");
}

// --- egress -------------------------------------------------------------------------------------

void test_send_queues_rather_than_notifying(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 512}};
    auto p = encryptedPacket();
    TEST_ASSERT_TRUE(h.onSend(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, h.notifyCalls, "Router::send must not block on the radio");
    h.pump();
    TEST_ASSERT_EQUAL(1, h.notified[1].size());
}

void test_send_fragments_per_peer_chunk_size(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 20}, {2, 200}};
    auto p = encryptedPacket(0x3061b02e, 0x04050b6e, 100);
    const auto encoded = encode(p);
    TEST_ASSERT_TRUE(h.onSend(&p));
    while (h.pump() == 10) {
    }

    const uint8_t small = BLEGattMeshHandler::fragmentCount(encoded.size(), 20);
    TEST_ASSERT_TRUE(small > 1);
    TEST_ASSERT_EQUAL(small, h.notified[1].size());
    TEST_ASSERT_EQUAL(1, h.notified[2].size());
    for (const auto &f : h.notified[1])
        TEST_ASSERT_TRUE(f.size() <= 20);

    // Both peers can rebuild the same bytes the router handed us.
    TEST_ASSERT_EQUAL(encoded.size(), reassembleAll(h.notified[1]).size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded.data(), reassembleAll(h.notified[1]).data(), encoded.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded.data(), reassembleAll(h.notified[2]).data(), encoded.size());

    // And a receiving handler decodes them into the original packet.
    FakeGattMesh rx;
    rx.start();
    for (const auto &f : h.notified[1])
        rx.feed(1, f);
    TEST_ASSERT_EQUAL(1, rx.received.size());
    TEST_ASSERT_EQUAL_UINT32(p.id, rx.received[0].id);
}

void test_a_relay_is_not_written_back_to_the_peer_it_came_from(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 512}, {2, 512}};
    h.feed(1, split(encode(encryptedPacket(0x3061b02e, 0x1234)), 1, 512)[0]);
    TEST_ASSERT_EQUAL(1, h.received.size());

    // What perhapsRebroadcast hands back: a copy of the received packet, still marked as it arrived.
    meshtastic_MeshPacket relay = h.received[0];
    TEST_ASSERT_TRUE(h.onSend(&relay));
    while (h.pump() == 10) {
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, h.notified[1].size(), "peer 1 already has this packet");
    TEST_ASSERT_EQUAL_MESSAGE(1, h.notified[2].size(), "peer 2 does not");
}

void test_an_origination_reaches_every_peer(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 512}, {2, 512}};
    auto p = encryptedPacket(0x3061b02e, 0x4321);
    TEST_ASSERT_TRUE(h.onSend(&p));
    while (h.pump() == 10) {
    }
    TEST_ASSERT_EQUAL(1, h.notified[1].size());
    TEST_ASSERT_EQUAL(1, h.notified[2].size());
}

void test_send_refuses_unencrypted_or_senderless_packets(void)
{
    FakeGattMesh h;
    h.start();
    auto plain = encryptedPacket();
    plain.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    TEST_ASSERT_FALSE(h.onSend(&plain));
    auto nobody = encryptedPacket(0 /* from */);
    TEST_ASSERT_FALSE(h.onSend(&nobody));
}

void test_tx_queue_is_bounded(void)
{
    FakeGattMesh h;
    h.start();
    size_t accepted = 0;
    for (size_t i = 0; i < BLE_GATT_MESH_TX_QUEUE_SIZE * 3; i++) {
        auto p = encryptedPacket(0x3061b02e, (uint32_t)(0x1000 + i));
        if (h.onSend(&p))
            accepted++;
    }
    TEST_ASSERT_EQUAL(BLE_GATT_MESH_TX_QUEUE_SIZE, accepted);
}

void test_a_busy_stack_is_retried_then_skipped(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 512}};
    h.busy = true;
    auto p = encryptedPacket();
    TEST_ASSERT_TRUE(h.onSend(&p));

    for (int i = 0; i < BLE_GATT_MESH_TX_ATTEMPTS - 1; i++)
        TEST_ASSERT_EQUAL_MESSAGE(10, h.pump(), "still retrying the same fragment");
    TEST_ASSERT_EQUAL(BLE_GATT_MESH_TX_ATTEMPTS - 1, h.notifyCalls);

    h.busy = false;
    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.notified[1].size(), "delivered once the stack accepts");

    // A peer that never accepts is skipped after the budget, so it cannot stall the ring forever.
    h.busy = true;
    h.notifyCalls = 0;
    auto q = encryptedPacket(0x3061b02e, 0x99);
    TEST_ASSERT_TRUE(h.onSend(&q));
    for (int i = 0; i < BLE_GATT_MESH_TX_ATTEMPTS; i++)
        h.pump();
    TEST_ASSERT_EQUAL(BLE_GATT_MESH_TX_ATTEMPTS, h.notifyCalls);
    h.busy = false;
    h.pump();
    TEST_ASSERT_EQUAL_MESSAGE(1, h.notified[1].size(), "the skipped packet is not resurrected");
}

void test_pump_waits_for_the_platform(void)
{
    FakeGattMesh h;
    h.start();
    h.peers = {{1, 512}};
    h.ready = false;
    auto p = encryptedPacket();
    TEST_ASSERT_TRUE(h.onSend(&p));
    h.pump();
    TEST_ASSERT_EQUAL(0, h.notifyCalls);
    h.ready = true;
    h.pump();
    TEST_ASSERT_EQUAL(1, h.notified[1].size());
}

void test_inbound_chunks_are_drained_by_the_pump(void)
{
    FakeGattMesh h;
    h.start();
    auto frags = split(encode(encryptedPacket(0x3061b02e, 0x77, 60)), 4, 40);
    for (const auto &f : frags)
        h.inbound.emplace_back(1, f);
    h.pump();
    TEST_ASSERT_EQUAL(1, h.received.size());
    TEST_ASSERT_TRUE(h.inbound.empty());
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    // deliverToRouter consults nodeDB to drop a peer claiming our own node number.
    if (!nodeDB)
        nodeDB = new NodeDB();
    UNITY_BEGIN();
    RUN_TEST(test_fragment_header_matches_the_client_format);
    RUN_TEST(test_fragment_count_bounds);
    RUN_TEST(test_parse_rejects_foreign_or_contradictory_headers);
    RUN_TEST(test_reassembles_a_fragmented_packet);
    RUN_TEST(test_a_full_size_packet_survives_the_smallest_chunk);
    RUN_TEST(test_single_fragment_completes_without_a_slot);
    RUN_TEST(test_a_gap_drops_the_assembly);
    RUN_TEST(test_a_repeated_fragment_is_a_no_op);
    RUN_TEST(test_in_flight_packets_per_peer_are_bounded);
    RUN_TEST(test_peers_holding_slots_are_bounded);
    RUN_TEST(test_half_built_packets_expire);
    RUN_TEST(test_a_lost_peer_forgets_its_assemblies);
    RUN_TEST(test_ingress_drops_a_packet_with_no_sender);
    RUN_TEST(test_ingress_drops_a_packet_claiming_to_be_us);
    RUN_TEST(test_ingress_drops_an_impossible_hop_count);
    RUN_TEST(test_ingress_clears_local_only_metadata);
    RUN_TEST(test_ingress_rejects_bytes_that_do_not_decode);
    RUN_TEST(test_send_queues_rather_than_notifying);
    RUN_TEST(test_send_fragments_per_peer_chunk_size);
    RUN_TEST(test_a_relay_is_not_written_back_to_the_peer_it_came_from);
    RUN_TEST(test_an_origination_reaches_every_peer);
    RUN_TEST(test_send_refuses_unencrypted_or_senderless_packets);
    RUN_TEST(test_tx_queue_is_bounded);
    RUN_TEST(test_a_busy_stack_is_retried_then_skipped);
    RUN_TEST(test_pump_waits_for_the_platform);
    RUN_TEST(test_inbound_chunks_are_drained_by_the_pump);
    exit(UNITY_END());
}

#else

void setup()
{
    initializeTestEnvironment();
    LOG_WARN("BLE GATT mesh tests require ARCH_PORTDUINO with HAS_BLE_GATT_MESH");
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif // ARCH_PORTDUINO && HAS_BLE_GATT_MESH

void loop() {}
