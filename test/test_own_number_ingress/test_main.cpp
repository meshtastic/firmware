// Received frames that carry this node's own number in `from`, through Router::perhapsHandleReceived()
// in src/mesh/Router.cpp, and the duplicate paths in FloodingRouter (reprocessPacket(),
// perhapsHandleUpgradedPacket()) that run before it.
//
// Why: every packet we transmit is recorded in PacketHistory when it is sent
// (NextHopRouter/FloodingRouter::send), so an echo heard back off the mesh is consumed by
// shouldFilterReceived(): that is where the implicit ACK fires and retransmissions stop. A frame with
// our number that the filter does not recognise (one of ours that has aged out of PacketHistory, or
// that was sent before a reboot) is not new inbound traffic. It must reach neither module dispatch nor
// the client, which would otherwise see it as a fresh packet from this node. MQTT and UDP ingress
// already drop from == us; this keeps LoRa ingress consistent with them.
//
// Regression guarded: delete the isFromUs() drop after shouldFilterReceived() and such a frame
// addressed to us is dispatched to modules and forwarded to the client, and a broadcast one reaches
// module dispatch (MessageStore, the screen). The first copy of such a frame still enters PacketHistory,
// so a repeated copy goes down the dupe path: delete the isFromUs() guards in FloodingRouter and it is
// reprocessed (a want_ack one is ACKed), and an "upgraded" copy evicts our own queued transmission,
// either in perhapsHandleUpgradedPacket() or, if that lets it fall through, in perhapsCancelDupe().
// The echo cases also follow the implicit ACK through the real RoutingModule::sendAckNak() to the phone
// queue, for a channel broadcast and for an opaque PKI DM: filtering our number at phone delivery
// (#11487) or skipping the opaque ACK (#11502) loses the client's "Delivered to mesh".
// Moving that drop ahead of the filter, or to the radio
// intake, instead breaks the echo case below: the implicit ACK and the retransmission cancel that our
// own rebroadcast heard back must still produce.

#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

// Inside the guard: the harness pulls in CryptoEngine entry points a PKI-excluded build lacks.
#include "support/AuthPipelineHarness.h"

// One id per case: the router and its PacketHistory outlive a test, so a reused (from,id) would be
// dropped as a dupe and pass for the wrong reason.
static constexpr PacketId REMOTE_ID = 0x5E1F0001;
static constexpr PacketId UNKNOWN_DM_ID = 0x5E1F0002;
static constexpr PacketId UNKNOWN_BROADCAST_ID = 0x5E1F0003;
static constexpr PacketId REPEATED_ID = 0x5E1F0004;
static constexpr PacketId UPGRADED_ID = 0x5E1F0005;
static constexpr PacketId UPGRADED_SPENT_ID = 0x5E1F0006;
static constexpr PacketId OUR_ID = 0x0A0A0001;
static constexpr PacketId OUR_PKI_DM_ID = 0x0A0A0002;

void setUp(void)
{
    pipelineHarnessSetUp();
}

void tearDown(void)
{
    pipelineHarnessTearDown();
}

// A channel-encrypted POSITION_APP frame (the port the harness module counts) as it arrives off LoRa.
static meshtastic_MeshPacket makeLoraFrame(NodeNum from, NodeNum to, PacketId id)
{
    meshtastic_MeshPacket p = makeDecoded(from, to, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.id = id;
    p.hop_limit = 1;
    p.hop_start = 2;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    return channelEncode(p);
}

// Control: an ordinary frame from another node addressed to us reaches module dispatch and the phone.
// Without it, the drops below could pass because this fixture delivers nothing at all.
void test_frame_from_another_node_reaches_modules_and_phone(void)
{
    runPipelineIngress(makeLoraFrame(REMOTE_NODE, LOCAL_NODE, REMOTE_ID));

    TEST_ASSERT_EQUAL_UINT32(1, pipelineModule->calls);
    TEST_ASSERT_EQUAL(1, drainPhoneQueue());
}

// from == to == our node number, not in PacketHistory: before the drop it reached modules and,
// because it is addressed to us, MeshService::handleFromRadio forwarded it to the client.
void test_unrecognised_frame_to_us_with_our_number_is_dropped(void)
{
    runPipelineIngress(makeLoraFrame(LOCAL_NODE, LOCAL_NODE, UNKNOWN_DM_ID));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineModule->calls, "an unrecognised own-number frame must not reach modules");
    TEST_ASSERT_EQUAL_MESSAGE(0, drainPhoneQueue(), "an unrecognised own-number frame must not reach the client");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRouting->ackCalls);
}

// A broadcast with our number, not in PacketHistory. The phone already skipped it
// (isFromUs && !isToUs), but modules did not: this pins the module-dispatch half.
void test_unrecognised_broadcast_with_our_number_is_dropped(void)
{
    runPipelineIngress(makeLoraFrame(LOCAL_NODE, NODENUM_BROADCAST, UNKNOWN_BROADCAST_ID));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineModule->calls, "an unrecognised own-number broadcast must not reach modules");
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRadio->sendCalls, "an own-number broadcast is never rebroadcast");
}

// The first copy of an unrecognised own-number frame enters PacketHistory before the drop, so a second
// copy is a dupe. A dupe with hop_start == hop_limit is treated as the originator retrying and is
// reprocessed (NextHopRouter::shouldFilterReceived), which for a want_ack frame to us sends an ACK.
// Reprocessing is never for our own packets: we do not relay them, and their implicit ACK is
// generated earlier, in ReliableRouter::shouldFilterReceived.
void test_repeated_own_number_frame_is_not_reprocessed(void)
{
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.id = REPEATED_ID;
    p.hop_limit = 2;
    p.hop_start = 2;
    p.want_ack = true;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    const meshtastic_MeshPacket frame = channelEncode(p);

    runPipelineIngress(frame);
    runPipelineIngress(frame);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRouting->ackCalls, "a repeated own-number frame must not be ACKed");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
}

// An own-number copy with more hops left than the one we sent looks like an "upgraded" duplicate.
// That path removes the lower-hop copy from the TX queue to relay the better one, but we never relay
// our own packets, so it must leave our queued transmission alone.
void test_upgraded_own_number_copy_keeps_our_queued_packet(void)
{
    meshtastic_MeshPacket ours = makeLoraFrame(LOCAL_NODE, NODENUM_BROADCAST, UPGRADED_ID);
    pipelineRouter->remember(&ours); // sent with hop_limit 1, still waiting in the TX queue
    pipelineRadio->holdInTxQueue(LOCAL_NODE, UPGRADED_ID);

    meshtastic_MeshPacket upgraded = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    upgraded.id = UPGRADED_ID;
    upgraded.hop_limit = 3;
    upgraded.hop_start = 3;
    upgraded.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    runPipelineIngress(channelEncode(upgraded));

    TEST_ASSERT_TRUE_MESSAGE(pipelineRadio->txQueueHolds(LOCAL_NODE, UPGRADED_ID), "our queued packet must stay queued");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->removeCalls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
}

// Same upgrade, but the copy has spent a hop on the way (hop_start > hop_limit). It is still a dupe, so
// if the upgrade path let it fall through it would reach perhapsCancelDupe(), which on a client cancels
// our queued transmission. The upgrade path must consume it.
void test_upgraded_own_number_copy_with_hops_spent_keeps_our_queued_packet(void)
{
    meshtastic_MeshPacket ours = makeLoraFrame(LOCAL_NODE, NODENUM_BROADCAST, UPGRADED_SPENT_ID);
    pipelineRouter->remember(&ours); // sent with hop_limit 1, still waiting in the TX queue
    pipelineRadio->holdInTxQueue(LOCAL_NODE, UPGRADED_SPENT_ID);

    meshtastic_MeshPacket upgraded = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    upgraded.id = UPGRADED_SPENT_ID;
    upgraded.hop_limit = 3;
    upgraded.hop_start = 4;
    upgraded.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    runPipelineIngress(channelEncode(upgraded));

    TEST_ASSERT_TRUE_MESSAGE(pipelineRadio->txQueueHolds(LOCAL_NODE, UPGRADED_SPENT_ID), "our queued packet must stay queued");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->cancelCalls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineRadio->removeCalls);
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, drainPhoneQueue());
}

// Drain the phone queue and require exactly one frame: the success ACK for `requestId`, as the client
// sees it ("Delivered to mesh"). Anything else there - the echo itself, a second ACK - fails.
static void expectOnlyImplicitAckOnPhone(PacketId requestId)
{
    int acks = 0, others = 0;
    while (meshtastic_MeshPacket *queued = pipelineService->getForPhone()) {
        meshtastic_Routing r = meshtastic_Routing_init_zero;
        const bool isAck =
            queued->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            queued->decoded.portnum == meshtastic_PortNum_ROUTING_APP && queued->decoded.request_id == requestId &&
            pb_decode_from_bytes(queued->decoded.payload.bytes, queued->decoded.payload.size, &meshtastic_Routing_msg, &r) &&
            r.which_variant == meshtastic_Routing_error_reason_tag && r.error_reason == meshtastic_Routing_Error_NONE;
        isAck ? acks++ : others++;
        packetPool.release(queued);
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, acks, "the implicit ACK must reach the phone with the original request_id");
    TEST_ASSERT_EQUAL_MESSAGE(0, others, "nothing but the ACK reaches the phone");
}

// A recognised echo: a neighbour rebroadcasts a want_ack broadcast we sent and are still
// retransmitting. It is in PacketHistory from our send, so shouldFilterReceived() handles it before
// the drop: the implicit ACK is generated and the pending retransmission cancelled. The ACK goes
// through the real RoutingModule::sendAckNak() -> Router::sendLocal() path, which does not pass the
// drop, so the client still gets its "Delivered to mesh" (the failure mode fixed in #11487).
void test_echo_of_our_own_packet_still_acks_implicitly(void)
{
    pipelineRouting->deliverAcks = true;
    meshtastic_MeshPacket ours = makeLoraFrame(LOCAL_NODE, NODENUM_BROADCAST, OUR_ID);
    ours.hop_limit = 2;
    ours.want_ack = true;
    pipelineRouter->remember(&ours); // what NextHopRouter/FloodingRouter::send records
    pipelineRouter->addPending(ours, UINT32_MAX);
    TEST_ASSERT_EQUAL(1, pipelineRouter->pendingCount());

    runPipelineIngress(makeRelayedCopy(ours));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineRouting->ackCalls, "our echo must still produce the implicit ACK");
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouter->pendingCount(), "our echo must still cancel the retransmission");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    expectOnlyImplicitAckOnPhone(OUR_ID);
}

// The opaque case: a want_ack PKI DM we sent, overheard being rebroadcast, cannot be decrypted by us.
// It takes the OPAQUE_RELAY_ONLY branch, which returns before shouldFilterReceived() and the drop, and
// generates the implicit ACK there (the failure mode fixed in #11502). That ACK must reach the phone.
void test_opaque_echo_of_our_pki_dm_delivers_implicit_ack_to_phone(void)
{
    pipelineRouting->deliverAcks = true;
    meshtastic_MeshPacket ours = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    ours.id = OUR_PKI_DM_ID;
    ours.want_ack = true;
    pipelineRouter->addPending(ours, UINT32_MAX);

    // As overheard: still ciphertext, on a channel hash we hold no key for, one hop spent.
    meshtastic_MeshPacket overheard = meshtastic_MeshPacket_init_zero;
    overheard.from = LOCAL_NODE;
    overheard.to = REMOTE_NODE;
    overheard.id = OUR_PKI_DM_ID;
    overheard.channel = 0x5A;
    overheard.hop_start = 3;
    overheard.hop_limit = 2;
    overheard.relay_node = 0x42;
    overheard.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    overheard.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    overheard.encrypted.size = 32;
    memset(overheard.encrypted.bytes, 0xC3, overheard.encrypted.size);

    runPipelineIngress(overheard);

    TEST_ASSERT_EQUAL_UINT32(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouter->pendingCount(), "our opaque echo must cancel the retransmission");
    TEST_ASSERT_EQUAL_UINT32(0, pipelineModule->calls);
    expectOnlyImplicitAckOnPhone(OUR_PKI_DM_ID);
}

void setup()
{
    pipelineHarnessCreate();
    UNITY_BEGIN();
    RUN_TEST(test_frame_from_another_node_reaches_modules_and_phone);
    RUN_TEST(test_unrecognised_frame_to_us_with_our_number_is_dropped);
    RUN_TEST(test_unrecognised_broadcast_with_our_number_is_dropped);
    RUN_TEST(test_repeated_own_number_frame_is_not_reprocessed);
    RUN_TEST(test_upgraded_own_number_copy_keeps_our_queued_packet);
    RUN_TEST(test_upgraded_own_number_copy_with_hops_spent_keeps_our_queued_packet);
    RUN_TEST(test_echo_of_our_own_packet_still_acks_implicitly);
    RUN_TEST(test_opaque_echo_of_our_pki_dm_delivers_implicit_ack_to_phone);
    const int result = UNITY_END();
    pipelineHarnessDestroy();
    exit(result);
}

void loop() {}

#else // XEdDSA or PKI excluded

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
