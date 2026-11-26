// meshtasticd testing knob: portduino_config.ignore_relay_nodes, applied in
// Router::perhapsHandleReceived() in src/mesh/Router.cpp.
//
// Why: a listed full node number is compared by its low byte with MeshPacket.relay_node, the byte of
// the node that transmitted the frame on air. Dropping it before shouldFilterReceived()/PacketHistory
// cuts exactly one radio link: a direct frame from that node is lost, but the same packet relayed by
// another node still arrives. config.lora.ignore_incoming cannot do this because it filters p->from,
// the originator, and therefore also discards copies relayed by other nodes.
//
// Regressions guarded:
//   - Move the check after shouldFilterReceived() and the relayed copy is dropped as a duplicate, so
//     the second case below loses the packet and the link cut becomes a node cut.
//   - Match the full node number instead of its low byte and nothing is ever dropped.
//   - Compare the raw low byte instead of getLastByteOfNodeNum() and a ...00 node is never dropped.
//   - Drop the NO_RELAY_NODE guard and an entry ending in 00 discards every direct packet.
//   - Drop the TRANSPORT_LORA guard and the same list also drops MQTT/UDP/API ingress.

#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if ARCH_PORTDUINO && !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

#include "platform/portduino/PortduinoGlue.h"
#include "support/AuthPipelineHarness.h"
#include <initializer_list>

static constexpr uint32_t LISTED_RELAYER = 0xA28E145B; // low byte 0x5b
static constexpr uint8_t LISTED_BYTE = 0x5b;
static constexpr uint8_t OTHER_BYTE = 0x7c;

// One id per case: the router and its PacketHistory outlive a test, so a reused (from,id) could be
// dropped as a dupe and pass for the wrong reason.
static constexpr PacketId DIRECT_ID = 0x1E1A0001;
static constexpr PacketId RELAYED_ID = 0x1E1A0002;
static constexpr PacketId MQTT_ID = 0x1E1A0003;
static constexpr PacketId EMPTY_ID = 0x1E1A0004;
static constexpr PacketId ZERO_BYTE_ID = 0x1E1A0005;
static constexpr PacketId ZERO_LISTED_ID = 0x1E1A0006;

void setUp(void)
{
    pipelineHarnessSetUp();
    portduino_config.ignore_relay_nodes.clear();
}

void tearDown(void)
{
    portduino_config.ignore_relay_nodes.clear();
    pipelineHarnessTearDown();
}

static void setIgnoreRelayNodes(std::initializer_list<uint32_t> nodes)
{
    portduino_config.ignore_relay_nodes.assign(nodes.begin(), nodes.end());
}

// A channel-encrypted POSITION_APP frame as it arrives off LoRa, with the relay byte the header carried.
static meshtastic_MeshPacket makeRelayedLoraFrame(NodeNum from, NodeNum to, PacketId id, uint8_t relay)
{
    meshtastic_MeshPacket p = makeDecoded(from, to, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.id = id;
    p.hop_limit = 1;
    p.hop_start = 2;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.relay_node = relay;
    return channelEncode(p);
}

void test_direct_frame_from_listed_relayer_is_dropped(void)
{
    setIgnoreRelayNodes({LISTED_RELAYER});
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, DIRECT_ID, LISTED_BYTE));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineModule->calls, "a listed relay's own frame must not reach modules");
    TEST_ASSERT_EQUAL_MESSAGE(0, drainPhoneQueue(), "a listed relay's own frame must not reach the client");
}

// The same packet, first heard through the listed relay and then through another node. The first copy
// must not enter PacketHistory, or the second is filtered as a duplicate.
void test_packet_relayed_by_unlisted_node_is_delivered(void)
{
    setIgnoreRelayNodes({LISTED_RELAYER});
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, RELAYED_ID, LISTED_BYTE));
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, RELAYED_ID, OTHER_BYTE));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineModule->calls, "the relayed copy must reach modules exactly once");
    TEST_ASSERT_EQUAL_MESSAGE(1, drainPhoneQueue(), "the relayed copy must reach the client exactly once");
}

void test_non_lora_transport_is_not_affected(void)
{
    setIgnoreRelayNodes({LISTED_RELAYER});
    meshtastic_MeshPacket p = makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, MQTT_ID, LISTED_BYTE);
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    runPipelineIngress(p);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineModule->calls, "a non-LoRa frame must not be dropped by this list");
    TEST_ASSERT_EQUAL_MESSAGE(1, drainPhoneQueue(), "a non-LoRa frame must reach the client");
}

void test_empty_relay_list_changes_nothing(void)
{
    setIgnoreRelayNodes({});
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, EMPTY_ID, LISTED_BYTE));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineModule->calls, "an empty list must deliver every frame");
    TEST_ASSERT_EQUAL_MESSAGE(1, drainPhoneQueue(), "an empty list must deliver every frame");
}

void test_no_relay_node_zero_never_matches(void)
{
    setIgnoreRelayNodes({0xA28E1400u});
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, ZERO_BYTE_ID, NO_RELAY_NODE));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineModule->calls, "relay_node 0 means no relay and must never match");
    TEST_ASSERT_EQUAL_MESSAGE(1, drainPhoneQueue(), "relay_node 0 means no relay and must never match");
}

// A node number ending in 00 is transmitted as relay byte 0xff, so listing it must drop 0xff; the
// no-relay value 0 stays unmatched.
void test_listed_zero_byte_node_drops_0xff(void)
{
    setIgnoreRelayNodes({0xA28E1400u});
    runPipelineIngress(makeRelayedLoraFrame(REMOTE_NODE, LOCAL_NODE, ZERO_LISTED_ID, 0xFF));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineModule->calls, "a listed ...00 node transmits as 0xff");
    TEST_ASSERT_EQUAL_MESSAGE(0, drainPhoneQueue(), "a listed ...00 node transmits as 0xff");
}

void setup()
{
    pipelineHarnessCreate();
    UNITY_BEGIN();
    RUN_TEST(test_direct_frame_from_listed_relayer_is_dropped);
    RUN_TEST(test_packet_relayed_by_unlisted_node_is_delivered);
    RUN_TEST(test_non_lora_transport_is_not_affected);
    RUN_TEST(test_empty_relay_list_changes_nothing);
    RUN_TEST(test_no_relay_node_zero_never_matches);
    RUN_TEST(test_listed_zero_byte_node_drops_0xff);
    const int result = UNITY_END();
    pipelineHarnessDestroy();
    exit(result);
}

void loop() {}

#else // not portduino, or PKI excluded

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
