// Unit tests for RepeatScalingModule, which decides how many duplicate rebroadcasts of a packet
// we have queued to rebroadcast ourselves we should tolerate before giving up:
//   - registerDupeHeard/clearDupeCount: the small ring buffer counting how many times we've
//     heard a duplicate of a packet we have queued to rebroadcast ourselves.
//   - getDupeCancelThreshold: the compile-time, per-portnum threshold (see
//     RepeatScalingModule.cpp) controlling how many duplicates we tolerate before giving up.
//   - meshTooBusyForExtraRepeats gate: channel/air utilization or direct-neighbor density
//     overriding any portnum-specific extra tolerance.
//   - shouldCancelDupe: the integration of all of the above - only actually gives up (and clears
//     its tracking state) once the configured threshold has been reached.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "modules/HopScalingModule.h"
#include "modules/RepeatScalingModule.h"

static constexpr NodeNum kSender1 = 0x000005AB;
static constexpr NodeNum kSender2 = 0x000006CD;
static constexpr PacketId kId1 = 0x1001;
static constexpr PacketId kId2 = 0x2002;

// ---------------------------------------------------------------------------
// Test shim - re-exposes the protected dupe-tracking helpers, and allows overriding
// getDupeCancelThreshold so tests don't depend on any real portnum having a non-default case.
// ---------------------------------------------------------------------------
class RepeatScalingModuleTestShim : public RepeatScalingModule
{
  public:
    using RepeatScalingModule::clearDupeCount;
    using RepeatScalingModule::registerDupeHeard;

    // Test-only override: bypasses the compile-time portnum switch so tests can exercise
    // shouldCancelDupe's threshold-gating without needing a real portnum case.
    uint8_t testThreshold = 1;
    uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p) override { return testThreshold; }
};

// A second, minimal shim that does NOT override getDupeCancelThreshold, for testing the real
// compile-time default implementation directly.
class RepeatScalingModulePlainShim : public RepeatScalingModule
{
  public:
    using RepeatScalingModule::getDupeCancelThreshold;
};

static RepeatScalingModuleTestShim *shim = nullptr;
static RepeatScalingModulePlainShim *plainShim = nullptr;

// ---------------------------------------------------------------------------
// Scoped helpers to simulate a busy mesh for meshTooBusyForExtraRepeats() (see
// RepeatScalingModule.cpp). Install a global airTime/hopScalingModule reporting the given
// condition for the enclosing scope, restoring the previous pointer on destruction. Mirrors
// test_traffic_management's ScopedBusyAirTime.
// ---------------------------------------------------------------------------
class ScopedChannelUtil
{
  public:
    explicit ScopedChannelUtil(float percent) : previous(airTime)
    {
        // channelUtilizationPercent() == sum(channelUtilization[]) / (PERIODS * 10 * 1000) * 100
        busy.channelUtilization[0] = static_cast<uint32_t>(percent / 100.0f * CHANNEL_UTILIZATION_PERIODS * 10 * 1000);
        airTime = &busy;
    }
    ~ScopedChannelUtil() { airTime = previous; }

  private:
    AirTime busy;
    AirTime *previous;
};

class ScopedAirUtilTx
{
  public:
    explicit ScopedAirUtilTx(float percent) : previous(airTime)
    {
        // utilizationTXPercent() == sum(utilizationTX[]) / MS_IN_HOUR * 100
        busy.utilizationTX[0] = static_cast<uint32_t>(percent / 100.0f * MS_IN_HOUR);
        airTime = &busy;
    }
    ~ScopedAirUtilTx() { airTime = previous; }

  private:
    AirTime busy;
    AirTime *previous;
};

#if HAS_VARIABLE_HOPS
class ScopedDirectActiveNodes
{
  public:
    explicit ScopedDirectActiveNodes(uint16_t count) : previous(hopScalingModule)
    {
        HopScalingModule::PerHopCounts counts;
        counts.perHop[0] = count;
        busy.setLastPerHopCountsForTest(counts);
        hopScalingModule = &busy;
    }
    ~ScopedDirectActiveNodes() { hopScalingModule = previous; }

  private:
    HopScalingModule busy;
    HopScalingModule *previous;
};
#endif

static meshtastic_MeshPacket makeDupePacket(NodeNum from, PacketId id)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.id = id;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag; // undecodable by default
    return p;
}

void setUp(void)
{
    shim->testThreshold = 1;
    // Clear any tracking state a previous test may have left behind for our fixed test keys.
    shim->clearDupeCount(kSender1, kId1);
    shim->clearDupeCount(kSender2, kId2);
}

void tearDown(void) {}

// ===========================================================================
// Group 1 - registerDupeHeard / clearDupeCount (the ring buffer)
// ===========================================================================

void test_registerDupeHeard_starts_at_one(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

void test_registerDupeHeard_increments_on_repeat(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender1, kId1));
    TEST_ASSERT_EQUAL_UINT8(3, shim->registerDupeHeard(kSender1, kId1));
}

void test_registerDupeHeard_independent_per_packet(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender1, kId1));
    // A different (sender, id) pair tracks its own independent count.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender2, kId2));
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender2, kId2));
    // The first pair's count is untouched by the second's activity.
    TEST_ASSERT_EQUAL_UINT8(3, shim->registerDupeHeard(kSender1, kId1));
}

void test_clearDupeCount_resets_tracking(void)
{
    shim->registerDupeHeard(kSender1, kId1);
    shim->registerDupeHeard(kSender1, kId1);
    shim->clearDupeCount(kSender1, kId1);
    // After clearing, the next duplicate heard for this packet starts fresh at 1.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

void test_clearDupeCount_of_untracked_packet_is_noop(void)
{
    // Clearing a (sender, id) that was never registered must not crash or disturb other entries.
    shim->registerDupeHeard(kSender1, kId1);
    shim->clearDupeCount(0xFFFFFFFF, 0xFFFFFFFF);
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender1, kId1));
}

void test_registerDupeHeard_ring_eviction_does_not_crash(void)
{
    // DUPE_COUNT_TRACKER_SIZE is 8 (see RepeatScalingModule.h); track more distinct packets than
    // that and confirm the ring buffer just evicts older entries rather than misbehaving.
    for (uint32_t i = 0; i < 20; i++) {
        uint8_t count = shim->registerDupeHeard(kSender1, 0x10000 + i);
        TEST_ASSERT_EQUAL_UINT8(1, count); // each is a fresh (sender, id) pair
    }
}

// ===========================================================================
// Group 2 - getDupeCancelThreshold (compile-time per-portnum default)
// ===========================================================================

void test_default_threshold_is_one_for_undecoded_packet(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_default_threshold_is_one_for_unlisted_portnum(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_POSITION_APP; // no case for this portnum
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_text_message_threshold_is_two(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

void test_text_message_compressed_threshold_is_two(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

void test_text_message_threshold_applies_regardless_of_addressing(void)
{
    // getDupeCancelThreshold only inspects portnum, not p->to - so a DM (to a specific node)
    // gets the same threshold as a broadcast for the same portnum. This is what lets
    // NextHopRouter::shouldFilterReceived's call to the same inherited
    // FloodingRouter::perhapsCancelDupe -> RepeatScalingModule::shouldCancelDupe path extend the
    // tolerance to DM text messages too, with no separate addressing-based gate.
    meshtastic_MeshPacket dm = makeDupePacket(kSender1, kId1);
    dm.to = 0x12345678; // a specific node, not NODENUM_BROADCAST
    dm.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    dm.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dm));
}

// ===========================================================================
// Group 3 - meshTooBusyForExtraRepeats gate (channel/air util, direct-neighbor density)
// ===========================================================================

void test_busy_channel_util_suppresses_text_message_threshold(void)
{
    ScopedChannelUtil busy(11.0f); // above the 10% BUSY_CHANNEL_UTIL_PERCENT gate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_channel_util_at_gate_does_not_suppress(void)
{
    ScopedChannelUtil notBusy(10.0f); // exactly at the threshold, not above it
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

void test_busy_air_util_tx_suppresses_text_message_threshold(void)
{
    ScopedAirUtilTx busy(5.0f); // above the 4% BUSY_AIR_UTIL_TX_PERCENT gate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_air_util_tx_at_gate_does_not_suppress(void)
{
    ScopedAirUtilTx notBusy(4.0f); // exactly at the threshold, not above it
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

#if HAS_VARIABLE_HOPS
void test_busy_direct_node_count_suppresses_text_message_threshold(void)
{
    ScopedDirectActiveNodes busy(11); // above the 10-node BUSY_DIRECT_ACTIVE_NODES gate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_direct_node_count_at_gate_does_not_suppress(void)
{
    ScopedDirectActiveNodes notBusy(10); // exactly at the threshold, not above it
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}
#endif

void test_busy_mesh_does_not_affect_already_unextended_portnums(void)
{
    // A portnum whose threshold is already 1 (the default floor) has nothing for the busy-mesh
    // gate to suppress - confirm it stays at 1, not some other value, while busy.
    ScopedChannelUtil busy(50.0f);
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

// ===========================================================================
// Group 4 - shouldCancelDupe (integration: threshold gates the cancel + clears tracking)
// ===========================================================================

void test_shouldCancelDupe_clears_tracking_once_threshold_reached(void)
{
    shim->testThreshold = 1; // historical behavior: give up (and clear) on the very first duplicate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    TEST_ASSERT_TRUE(shim->shouldCancelDupe(&p));

    // Threshold (1) was reached on the first call, so tracking was cleared - the next duplicate
    // heard for this packet starts a fresh count, rather than continuing to accumulate.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

void test_shouldCancelDupe_waits_for_configured_repeats(void)
{
    shim->testThreshold = 3; // this node should tolerate hearing 2 repeats before giving up
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    TEST_ASSERT_FALSE(shim->shouldCancelDupe(&p)); // 1st duplicate heard -> internal count 1, below threshold

    // Directly registering one more duplicate both (a) advances the count to 2 for the next
    // step, and (b) proves tracking was NOT cleared by the call above: a cleared entry would
    // restart at 1, not continue to 2.
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender1, kId1));

    TEST_ASSERT_TRUE(shim->shouldCancelDupe(&p)); // 3rd duplicate heard -> internal count 3, reaches threshold
    // Threshold reached inside the call above, so tracking was cleared - starts fresh at 1.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    shim = new RepeatScalingModuleTestShim();
    plainShim = new RepeatScalingModulePlainShim();

    printf("\n=== registerDupeHeard / clearDupeCount (ring buffer) ===\n");
    RUN_TEST(test_registerDupeHeard_starts_at_one);
    RUN_TEST(test_registerDupeHeard_increments_on_repeat);
    RUN_TEST(test_registerDupeHeard_independent_per_packet);
    RUN_TEST(test_clearDupeCount_resets_tracking);
    RUN_TEST(test_clearDupeCount_of_untracked_packet_is_noop);
    RUN_TEST(test_registerDupeHeard_ring_eviction_does_not_crash);

    printf("\n=== getDupeCancelThreshold (compile-time default) ===\n");
    RUN_TEST(test_default_threshold_is_one_for_undecoded_packet);
    RUN_TEST(test_default_threshold_is_one_for_unlisted_portnum);
    RUN_TEST(test_text_message_threshold_is_two);
    RUN_TEST(test_text_message_compressed_threshold_is_two);
    RUN_TEST(test_text_message_threshold_applies_regardless_of_addressing);

    printf("\n=== meshTooBusyForExtraRepeats gate ===\n");
    RUN_TEST(test_busy_channel_util_suppresses_text_message_threshold);
    RUN_TEST(test_channel_util_at_gate_does_not_suppress);
    RUN_TEST(test_busy_air_util_tx_suppresses_text_message_threshold);
    RUN_TEST(test_air_util_tx_at_gate_does_not_suppress);
#if HAS_VARIABLE_HOPS
    RUN_TEST(test_busy_direct_node_count_suppresses_text_message_threshold);
    RUN_TEST(test_direct_node_count_at_gate_does_not_suppress);
#endif
    RUN_TEST(test_busy_mesh_does_not_affect_already_unextended_portnums);

    printf("\n=== shouldCancelDupe (threshold gating) ===\n");
    RUN_TEST(test_shouldCancelDupe_clears_tracking_once_threshold_reached);
    RUN_TEST(test_shouldCancelDupe_waits_for_configured_repeats);

    exit(UNITY_END());
}

void loop() {}
