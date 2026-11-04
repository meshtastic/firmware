// LoRaConfig.transparent_mode: the node is a dumb LoRa modem for a client that runs the mesh itself.
//
// Under test:
//   - Router::perhapsHandleReceived() in src/mesh/Router.cpp, which hands every LoRa frame to
//     MeshService::handleTransparentFromRadio() before any filter, decrypt, history, module or relay step.
//   - MeshService::handleToRadio() / handleTransparentToRadio() in src/mesh/MeshService.cpp and
//     Router::sendTransparent(), which put a client's encrypted frame on the radio with its header as given.
//   - The transparent_mode gates in Router::sendLocal() and Router::send(), which keep anything generated
//     on-device off the air.
//   - The early return in PhoneAPI::handleToRadioPacket() (src/mesh/PhoneAPI.cpp) that keeps a client
//     frame clear of the decoded-payload policy there.
//
// Why: the client owns encryption, routing, dedup and retries. A frame the firmware decrypts, drops as a
// dupe, relays, ACKs or rewrites (from, id, relay_node, next_hop, hop fields) corrupts the client's mesh
// state, and a frame the node itself originates lands on a channel the client never configured.
// MeshPacket with an `encrypted` payload carries the 16-byte radio header field for field, so that is the
// frame format in both directions.
//
// Regressions guarded:
//   - Drop the RX early return and a decodable frame reaches the client decoded, is relayed, enters
//     PacketHistory, and a repeated or own-number frame is filtered out.
//   - Route client frames through Router::send() and from/relay_node/next_hop are overwritten with ours, a
//     want_ack frame starts firmware retransmissions, and the frame enters PacketHistory.
//   - Drop the PhoneAPI early return and a client retry that reuses an id is dropped as "seen recently",
//     and a frame whose ciphertext length equals a rate-limited portnum (decoded.portnum aliases
//     encrypted.size in the union) is rate limited.
//   - Drop the sendLocal()/send() gates and the node's own NodeInfo/telemetry/position go out on the air.
//   - Apply the duty-cycle check to client frames and the firmware second-guesses the client, which owns
//     airtime rules in this mode: frames are refused once the node's own hourly TX budget reads as spent.

#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

#include "mesh/StreamAPI.h"
#include "support/AuthPipelineHarness.h"

// Ids for the mode-off control only: the router and its PacketHistory outlive a test, so a frame the
// control processed would read as "in history" in the transparent cases.
static constexpr PacketId CONTROL_ID = 0x7A000101;
static constexpr PacketId RX_ID = 0x7A000001;
static constexpr PacketId TX_ID = 0x7A000002;
static constexpr PacketId TX_ID_2 = 0x7A000003;
static constexpr uint8_t FOREIGN_CHANNEL_HASH = 0x42;

class TestStreamAPI : public StreamAPI
{
  public:
    TestStreamAPI() : StreamAPI(nullptr) {}
    bool checkIsConnected() override { return true; }
};

static TestStreamAPI *streamAPI = nullptr;

void setUp(void)
{
    pipelineHarnessSetUp();
    config.lora.transparent_mode = true;
    streamAPI = new TestStreamAPI();
}

void tearDown(void)
{
    delete streamAPI;
    streamAPI = nullptr;
    pipelineHarnessTearDown();
}

// A frame encrypted on our own primary channel: the normal pipeline would decode, relay and dispatch it.
static meshtastic_MeshPacket makeDecodableLoraFrame(NodeNum from, NodeNum to, PacketId id)
{
    meshtastic_MeshPacket p = makeDecoded(from, to, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.id = id;
    p.hop_limit = 2;
    p.hop_start = 3;
    p.want_ack = !isBroadcast(to);
    p.relay_node = 0x33;
    p.next_hop = 0x44;
    p.rx_snr = 6.5f;
    p.rx_rssi = -80;
    p.has_rx_rssi = true;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    return channelEncode(p);
}

// A client-built frame: every header field set to something the normal send path would rewrite.
static meshtastic_MeshPacket makeClientFrame(PacketId id, pb_size_t payloadLen)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = REMOTE_NODE;
    p.to = ADMIN_NODE;
    p.id = id;
    p.channel = FOREIGN_CHANNEL_HASH;
    p.hop_limit = 2;
    p.hop_start = 5;
    p.want_ack = true;
    p.via_mqtt = true;
    p.next_hop = 0x0D;
    p.relay_node = 0x0B;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = payloadLen;
    for (pb_size_t i = 0; i < payloadLen; i++)
        p.encrypted.bytes[i] = (uint8_t)(0xC0 ^ i);
    return p;
}

static void assertSameFrame(const meshtastic_MeshPacket &expected, const meshtastic_MeshPacket &actual)
{
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, actual.which_payload_variant);
    TEST_ASSERT_EQUAL_HEX32(expected.from, actual.from);
    TEST_ASSERT_EQUAL_HEX32(expected.to, actual.to);
    TEST_ASSERT_EQUAL_HEX32(expected.id, actual.id);
    TEST_ASSERT_EQUAL_HEX8(expected.channel, actual.channel);
    TEST_ASSERT_EQUAL_UINT8(expected.hop_limit, actual.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(expected.hop_start, actual.hop_start);
    TEST_ASSERT_EQUAL(expected.want_ack, actual.want_ack);
    TEST_ASSERT_EQUAL(expected.via_mqtt, actual.via_mqtt);
    TEST_ASSERT_EQUAL_HEX8(expected.next_hop, actual.next_hop);
    TEST_ASSERT_EQUAL_HEX8(expected.relay_node, actual.relay_node);
    TEST_ASSERT_EQUAL_UINT32(expected.encrypted.size, actual.encrypted.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.encrypted.bytes, actual.encrypted.bytes, expected.encrypted.size);
}

/// Pop the next QueueStatus for the client; assert it reports `id` and return its result code.
static int32_t takeQueueStatus(PacketId id)
{
    meshtastic_QueueStatus *qs = pipelineService->getQueueStatusForPhone();
    TEST_ASSERT_NOT_NULL_MESSAGE(qs, "every client frame must be answered with a QueueStatus");
    TEST_ASSERT_EQUAL_HEX32(id, qs->mesh_packet_id);
    const int32_t res = qs->res;
    pipelineService->releaseQueueStatusToPool(qs);
    return res;
}

static bool sendToRadioViaApi(const meshtastic_MeshPacket &p)
{
    meshtastic_ToRadio message = meshtastic_ToRadio_init_zero;
    message.which_payload_variant = meshtastic_ToRadio_packet_tag;
    message.packet = p;
    uint8_t encoded[meshtastic_ToRadio_size] = {};
    const size_t encodedSize = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ToRadio_msg, &message);
    TEST_ASSERT_GREATER_THAN(0, encodedSize);
    return streamAPI->handleToRadio(encoded, encodedSize);
}

// Control: with the mode off, a frame to us is decoded and dispatched, and a channel broadcast is relayed.
// Without this the transparent cases could pass because the fixture never processes anything.
void test_mode_off_frames_are_processed_normally(void)
{
    config.lora.transparent_mode = false;
    runPipelineIngress(makeDecodableLoraFrame(REMOTE_NODE, LOCAL_NODE, CONTROL_ID));
    runPipelineIngress(makeChannelBroadcastFrom(REMOTE_NODE, NODENUM_BROADCAST, CONTROL_ID + 1));

    TEST_ASSERT_EQUAL_UINT32(1, pipelineModule->calls);
    TEST_ASSERT_EQUAL_UINT32(1, pipelineRadio->sendCalls);
    meshtastic_MeshPacket *delivered = pipelineService->getForPhone();
    TEST_ASSERT_NOT_NULL(delivered);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_decoded_tag, delivered->which_payload_variant);
    packetPool.release(delivered);
}

// The frame reaches the client byte for byte, still encrypted, with the received channel hash and route
// bytes, and the node does nothing else with it: no module, no relay, no ACK, no history.
void test_rx_frames_reach_client_raw_and_nothing_else(void)
{
    const meshtastic_MeshPacket toUs = makeDecodableLoraFrame(REMOTE_NODE, LOCAL_NODE, RX_ID);
    const meshtastic_MeshPacket broadcast = makeChannelBroadcastFrom(REMOTE_NODE, NODENUM_BROADCAST, RX_ID + 1);
    runPipelineIngress(toUs);
    runPipelineIngress(broadcast);

    meshtastic_MeshPacket *delivered = pipelineService->getForPhone();
    TEST_ASSERT_NOT_NULL(delivered);
    assertSameFrame(toUs, *delivered);
    TEST_ASSERT_EQUAL_FLOAT(toUs.rx_snr, delivered->rx_snr);
    TEST_ASSERT_EQUAL_INT32(toUs.rx_rssi, delivered->rx_rssi);
    packetPool.release(delivered);
    delivered = pipelineService->getForPhone();
    TEST_ASSERT_NOT_NULL(delivered);
    assertSameFrame(broadcast, *delivered);
    packetPool.release(delivered);
    TEST_ASSERT_NULL(pipelineService->getForPhone());

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineModule->calls, "modules must not see the frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRadio->sendCalls, "the node must not relay");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRouting->ackCalls, "the node must not ACK a want_ack frame to it");
    TEST_ASSERT_FALSE_MESSAGE(pipelineRouter->historyContains(&toUs), "the frame must not enter PacketHistory");
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&broadcast));
}

// Frames the normal pipeline filters (a repeat, one carrying our own node number) still reach the client:
// telling a retry from a duplicate is the client's call.
void test_rx_repeated_and_own_number_frames_still_reach_client(void)
{
    const meshtastic_MeshPacket frame = makeChannelBroadcastFrom(REMOTE_NODE, NODENUM_BROADCAST, RX_ID);
    runPipelineIngress(frame);
    runPipelineIngress(frame);
    runPipelineIngress(makeChannelBroadcastFrom(LOCAL_NODE, NODENUM_BROADCAST, RX_ID + 1));

    TEST_ASSERT_EQUAL(3, drainPhoneQueue());
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
}

// MQTT/UDP ingress is not something the radio heard; in this mode it has nowhere to go.
void test_rx_non_lora_ingress_is_dropped(void)
{
    meshtastic_MeshPacket frame = makeChannelBroadcastFrom(REMOTE_NODE, NODENUM_BROADCAST, RX_ID);
    frame.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    runPipelineIngress(frame);

    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
}

// A client frame goes out with every header field as given. The local scheduling hints a client could
// use to hold the TX queue are cleared; a want_ack frame starts no firmware retransmission.
void test_tx_client_frame_goes_out_verbatim(void)
{
    meshtastic_MeshPacket frame = makeClientFrame(TX_ID, 32);
    frame.tx_after = 0xFFFFFFF0;
    frame.rx_snr = 9.0f;
    pipelineService->handleToRadio(frame);

    TEST_ASSERT_EQUAL_UINT32(1, pipelineRadio->sendCalls);
    const meshtastic_MeshPacket *sent = pipelineRadio->lastSent();
    assertSameFrame(makeClientFrame(TX_ID, 32), *sent);
    TEST_ASSERT_EQUAL_UINT32(0, sent->tx_after);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sent->rx_snr);
    TEST_ASSERT_EQUAL_INT32(ERRNO_OK, takeQueueStatus(TX_ID));
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouter->pendingCount(), "retries belong to the client");
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(sent));
    TEST_ASSERT_EQUAL_MESSAGE(0, drainPhoneQueue(), "nothing is looped back to the client");
}

// Through the PhoneAPI: a retry with the same id, and frames whose 3-byte ciphertext reads as
// POSITION_APP through the union (10 s rate limit), all go out.
void test_tx_api_frames_skip_dedup_and_portnum_rate_limits(void)
{
    const meshtastic_MeshPacket first = makeClientFrame(TX_ID, (pb_size_t)meshtastic_PortNum_POSITION_APP);
    const meshtastic_MeshPacket second = makeClientFrame(TX_ID_2, (pb_size_t)meshtastic_PortNum_POSITION_APP);

    TEST_ASSERT_TRUE(sendToRadioViaApi(first));
    TEST_ASSERT_TRUE(sendToRadioViaApi(first));
    TEST_ASSERT_TRUE(sendToRadioViaApi(second));

    TEST_ASSERT_EQUAL_UINT32(3, pipelineRadio->sendCalls);
    TEST_ASSERT_EQUAL_UINT32(2, pipelineRadio->sentCountFor(REMOTE_NODE, TX_ID));
    TEST_ASSERT_EQUAL_UINT32(1, pipelineRadio->sentCountFor(REMOTE_NODE, TX_ID_2));
}

// A header with no sender cannot go out (receivers drop it; the radio encoder asserts on it), nor can a
// payload past the radio's capacity or a hop field wider than its 3 header bits (the encoder would rewrite
// it). Each is refused with a result the client can act on rather than sent altered.
void test_tx_unsendable_client_frames_are_refused(void)
{
    meshtastic_MeshPacket noSender = makeClientFrame(TX_ID, 16);
    noSender.from = 0;
    pipelineService->handleToRadio(noSender);
    TEST_ASSERT_EQUAL_INT32(meshtastic_Routing_Error_BAD_REQUEST, takeQueueStatus(TX_ID));

    meshtastic_MeshPacket tooBig = makeClientFrame(TX_ID_2, (pb_size_t)(MAX_RADIO_PAYLOAD_LEN + 1));
    pipelineService->handleToRadio(tooBig);
    TEST_ASSERT_EQUAL_INT32(meshtastic_Routing_Error_TOO_LARGE, takeQueueStatus(TX_ID_2));

    meshtastic_MeshPacket hopOverflow = makeClientFrame(TX_ID + 2, 16);
    hopOverflow.hop_limit = HOP_MAX + 1;
    pipelineService->handleToRadio(hopOverflow);
    TEST_ASSERT_EQUAL_INT32(meshtastic_Routing_Error_BAD_REQUEST, takeQueueStatus(TX_ID + 2));

    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
}

// The node encrypts nothing in this mode, so a decoded packet for the mesh is refused rather than sent in
// the clear or on a channel the client did not pick; it is not looped back either.
void test_tx_decoded_packet_for_mesh_is_refused(void)
{
    meshtastic_MeshPacket text = makeDecoded(0, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    text.id = TX_ID;
    pipelineService->handleToRadio(text);

    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
    TEST_ASSERT_EQUAL_INT32(ERRNO_DISABLED, takeQueueStatus(TX_ID));
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
}

// A decoded packet addressed to this node is still handled locally: it is how the client sets the
// LoRaConfig (and everything else) over admin.
void test_tx_decoded_packet_to_this_node_is_still_handled(void)
{
    meshtastic_MeshPacket toUs = makeDecoded(0, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    toUs.id = TX_ID;
    pipelineService->handleToRadio(toUs);

    TEST_ASSERT_EQUAL_UINT32(1, pipelineModule->calls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
}

// What the node generates on its own (module broadcasts through sendLocal(), direct Router::send() callers
// such as the beacon) stays off the air, a local broadcast is not looped back to modules or the client, and
// the client is not sent QueueStatus noise for ids it never sent.
void test_on_device_traffic_never_reaches_radio(void)
{
    meshtastic_MeshPacket *broadcast = pipelineRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(broadcast);
    broadcast->decoded.portnum = meshtastic_PortNum_POSITION_APP;
    TEST_ASSERT_EQUAL_INT32(ERRNO_DISABLED, pipelineRouter->sendLocal(broadcast, RX_SRC_LOCAL));

    meshtastic_MeshPacket *direct = pipelineRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(direct);
    TEST_ASSERT_EQUAL_INT32(ERRNO_DISABLED, pipelineRouter->send(direct));

    // A module send through the service: the client never sent this id, so it gets no QueueStatus for it.
    meshtastic_MeshPacket *moduleSend = pipelineRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(moduleSend);
    TEST_ASSERT_EQUAL_INT32(ERRNO_DISABLED, pipelineService->sendToMesh(moduleSend, RX_SRC_LOCAL));
    TEST_ASSERT_NULL(pipelineService->getQueueStatusForPhone());

    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
}

// EU_866 allows 2.5% TX per hour; log an hour's worth of 10% so the node's own budget reads as spent.
// Control: an on-device send with the mode off is refused for duty cycle, proving the budget is spent.
// Then a client frame in transparent mode still goes out. Runs last: the logged airtime outlives the test
// (tearDown puts the region back to US, which has no duty-cycle limit).
void test_tx_client_frame_ignores_duty_cycle(void)
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    initRegion();
    airTime->logAirtime(TX_LOG, 360000);

    config.lora.transparent_mode = false;
    meshtastic_MeshPacket *own = pipelineRouter->allocForSending();
    TEST_ASSERT_NOT_NULL(own);
    TEST_ASSERT_EQUAL_INT32(meshtastic_Routing_Error_DUTY_CYCLE_LIMIT, pipelineRouter->send(own));
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->sendCalls);

    config.lora.transparent_mode = true;
    meshtastic_MeshPacket frame = makeClientFrame(TX_ID, 32);
    pipelineService->handleToRadio(frame);
    TEST_ASSERT_EQUAL_UINT32(1, pipelineRadio->sendCalls);
    TEST_ASSERT_EQUAL_INT32(ERRNO_OK, takeQueueStatus(TX_ID));
}

void setup()
{
    pipelineHarnessCreate();
    UNITY_BEGIN();
    RUN_TEST(test_mode_off_frames_are_processed_normally);
    RUN_TEST(test_rx_frames_reach_client_raw_and_nothing_else);
    RUN_TEST(test_rx_repeated_and_own_number_frames_still_reach_client);
    RUN_TEST(test_rx_non_lora_ingress_is_dropped);
    RUN_TEST(test_tx_client_frame_goes_out_verbatim);
    RUN_TEST(test_tx_api_frames_skip_dedup_and_portnum_rate_limits);
    RUN_TEST(test_tx_unsendable_client_frames_are_refused);
    RUN_TEST(test_tx_decoded_packet_for_mesh_is_refused);
    RUN_TEST(test_tx_decoded_packet_to_this_node_is_still_handled);
    RUN_TEST(test_on_device_traffic_never_reaches_radio);
    RUN_TEST(test_tx_client_frame_ignores_duty_cycle); // last: leaves TX airtime logged
    const int result = UNITY_END();
    pipelineHarnessDestroy();
    exit(result);
}

void loop() {}

#else // XEdDSA or PKI excluded: the ingress harness needs both

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
