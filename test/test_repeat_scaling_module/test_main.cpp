// RepeatScalingModule in src/modules/RepeatScalingModule.cpp: the (sender, id) ring, the per-portnum
// threshold with its plaintext-header fallback, the busy-mesh gate, the noteScheduled() portnum cache,
// and shouldCancelDupe() over all of them.
//
// Why: text messages have no broadcast ACK or retry, so a relay waits for a second heard duplicate
// before dropping its own copy. A packet whose portnum is unknown gets the same tolerance only when
// its header shows a flooded DM (to one node, NO_NEXT_HOP_PREFERENCE); every other packet keeps
// today's cancel-on-first. On a busy mesh (channel or TX airtime, or too many estimated direct
// neighbours) the extra repeat costs more than it saves, so every threshold drops back to 1.
// Duplicates arrive encrypted, so the portnum cached when we queued our copy is the only way a
// duplicate can be recognised as text.
//
// Regressions guarded: text duplicates cancelling on the first copy again (lost or evicted cache
// entries, or the cache filling with bulk broadcast relays); extra repeats on a busy or dense mesh,
// including a dense mesh hidden by HopScalingModule's hash subsampling; and tracker slots claimed or
// leaked by packets that cannot need them.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "airtime.h"
#include "configuration.h"
#include "mesh/RadioInterface.h"
#include "modules/HopScalingModule.h"
#include "modules/RepeatScalingModule.h"

static constexpr NodeNum kSender1 = 0x000005AB;
static constexpr NodeNum kSender2 = 0x000006CD;
static constexpr PacketId kId1 = 0x1001;
static constexpr PacketId kId2 = 0x2002;

// Re-exposes the protected helpers and overrides getDupeCancelThreshold so threshold-gating can be
// tested without depending on a real portnum case.
class RepeatScalingModuleTestShim : public RepeatScalingModule
{
  public:
    using RepeatScalingModule::clearDupeCount;
    using RepeatScalingModule::lookupNotedPortnum;
    using RepeatScalingModule::registerDupeHeard;

    uint8_t testThreshold = 1;
    uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p) override { return testThreshold; }
};

// A second, minimal shim that does NOT override getDupeCancelThreshold, for testing the real
// compile-time default implementation directly.
class RepeatScalingModulePlainShim : public RepeatScalingModule
{
  public:
    using RepeatScalingModule::clearDupeCount;
    using RepeatScalingModule::getDupeCancelThreshold;
    using RepeatScalingModule::registerDupeHeard;
};

// Substitutes the busy/dense decision itself, so the behaviour that depends on it can be tested
// without driving airTime/hopScalingModule.
class RepeatScalingModuleBusyShim : public RepeatScalingModule
{
  public:
    using RepeatScalingModule::getDupeCancelThreshold;

    bool testMeshBusy = false;
    bool meshTooBusyForExtraRepeats() override { return testMeshBusy; }
};

static RepeatScalingModuleTestShim *shim = nullptr;
static constexpr uint32_t kRing = RepeatScalingModule::DUPE_COUNT_TRACKER_SIZE;
static RepeatScalingModulePlainShim *plainShim = nullptr;

// Scoped helpers that install a global airTime/hopScalingModule reporting a busy condition for the
// enclosing scope, restoring the previous pointer on destruction (mirrors ScopedBusyAirTime). A failed
// assertion longjmps past that destructor, so setUp() also reinstates the suite's original pointers.
class ScopedChannelUtil
{
  public:
    explicit ScopedChannelUtil(float percent) : previous(airTime)
    {
        // AirTime's buckets are private, so drive them through the public write path: every report
        // type sums into channelUtilization[], and RX_ALL_LOG touches nothing else.
        busy.logAirtime(RX_ALL_LOG, static_cast<uint32_t>(percent / 100.0f * CHANNEL_UTILIZATION_PERIODS * 10 * 1000));
        airTime = &busy;
    }
    ~ScopedChannelUtil() { airTime = previous; }

  private:
    AirTime busy;
    AirTime *previous;
};

// No ScopedAirUtilTx: TX_LOG also feeds channel util, which trips first; meshTooBusy() covers that arm.

#if HAS_VARIABLE_HOPS
class ScopedDirectActiveNodes
{
  public:
    explicit ScopedDirectActiveNodes(uint16_t count, uint8_t denominator = 1) : previous(hopScalingModule)
    {
        HopScalingModule::PerHopCounts counts;
        counts.perHop[0] = count;
        counts.denominator = denominator;
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
    p.to = NODENUM_BROADCAST;            // tests override for DM shapes
    p.next_hop = NO_NEXT_HOP_PREFERENCE; // flooded unless a test pins a relay byte
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag; // undecodable by default
    return p;
}

static AirTime *suiteAirTime = nullptr;
#if HAS_VARIABLE_HOPS
static HopScalingModule *suiteHopScalingModule = nullptr;
#endif

void setUp(void)
{
    airTime = suiteAirTime;
#if HAS_VARIABLE_HOPS
    hopScalingModule = suiteHopScalingModule;
#endif
    shim->testThreshold = 1;
    // Clear any tracking state a previous test may have left behind for our fixed test keys.
    shim->clearDupeCount(kSender1, kId1);
    shim->clearDupeCount(kSender2, kId2);
    plainShim->clearDupeCount(kSender1, kId1);
    plainShim->clearDupeCount(kSender2, kId2);
}

void tearDown(void) {}

// Group 1 - registerDupeHeard / clearDupeCount (the ring buffer)

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
    // Track more distinct packets than DUPE_COUNT_TRACKER_SIZE and confirm the ring buffer just
    // evicts older entries rather than misbehaving.
    for (uint32_t i = 0; i < 2u * RepeatScalingModule::DUPE_COUNT_TRACKER_SIZE + 4; i++) {
        uint8_t count = shim->registerDupeHeard(kSender1, 0x10000 + i);
        TEST_ASSERT_EQUAL_UINT8(1, count); // each is a fresh (sender, id) pair
    }
}

// Queued rebroadcasts fill the ring; freeing one must not cost any of the others their portnum.
void test_claim_reuses_a_cleared_slot_before_evicting_a_live_one(void)
{
    RepeatScalingModuleTestShim ring;
    for (uint32_t i = 0; i < kRing; i++)
        ring.noteScheduled(kSender1, 0x20000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);
    ring.clearDupeCount(kSender1, 0x20003);

    ring.noteScheduled(kSender2, kId2, meshtastic_PortNum_TEXT_MESSAGE_APP);

    for (uint32_t i = 0; i < kRing; i++) {
        if (i != 3)
            TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender1, 0x20000 + i));
    }
    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender2, kId2));
}

// A reused slot is a new claim: three later claims on a full ring must evict old entries, not it.
void test_full_ring_evicts_the_oldest_claim_after_reuse(void)
{
    RepeatScalingModuleTestShim ring;
    for (uint32_t i = 0; i < kRing; i++)
        ring.noteScheduled(kSender1, 0x50000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);
    ring.clearDupeCount(kSender1, 0x50002);
    ring.noteScheduled(kSender2, 0x60000, meshtastic_PortNum_TEXT_MESSAGE_APP); // reuses the cleared slot

    for (uint32_t i = 1; i <= 3; i++) // ring full: these evict the three oldest claims
        ring.noteScheduled(kSender2, 0x60000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);

    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender2, 0x60000));
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, 0x50000));
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, 0x50001));
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, 0x50003));
    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender1, 0x50004));
}

// A duplicate heard before we (re)schedule claims the slot first; scheduling must make it the newest claim.
void test_noteScheduled_on_existing_entry_refreshes_its_claim(void)
{
    RepeatScalingModuleTestShim ring;
    ring.registerDupeHeard(kSender2, kId2); // oldest claim
    for (uint32_t i = 1; i < kRing; i++)
        ring.noteScheduled(kSender1, 0x90000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);
    ring.noteScheduled(kSender2, kId2, meshtastic_PortNum_TEXT_MESSAGE_APP);

    ring.noteScheduled(kSender1, 0x90000 + kRing, meshtastic_PortNum_TEXT_MESSAGE_APP); // ring full: evicts one

    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender2, kId2));
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, 0x90001));
}

// A threshold-1 duplicate cancels at once, so a burst of them must not evict the queued text packets' entries.
void test_threshold_one_duplicates_do_not_claim_ring_slots(void)
{
    RepeatScalingModuleTestShim ring;
    for (uint32_t i = 0; i < kRing; i++)
        ring.noteScheduled(kSender1, 0x30000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);

    ring.testThreshold = 1;
    for (uint32_t i = 0; i < 20; i++) {
        meshtastic_MeshPacket other = makeDupePacket(kSender2, 0x40000 + i);
        TEST_ASSERT_TRUE(ring.shouldCancelDupe(&other));
    }

    for (uint32_t i = 0; i < kRing; i++)
        TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender1, 0x30000 + i));
}

// Group 2 - getDupeCancelThreshold (compile-time per-portnum default)

void test_default_threshold_is_one_for_undecoded_directed_packet(void)
{
    // Portnum unknowable (encrypted, no noteScheduled cache hit) falls back to the next_hop gate
    // (see Group 2b below) - a directed route (specific next_hop byte, not flooding) keeps the
    // historical default of 1.
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.next_hop = 0x42; // a specific, directed relay byte - not NO_NEXT_HOP_PREFERENCE
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

// Group 2b - undecodable packets, judged from the plaintext to/next_hop header

// A route-less DM (specific node, NO_NEXT_HOP_PREFERENCE) - the shape of a relayed PKI DM we can't
// decrypt, carrying the same next_hop as any broadcast.
static meshtastic_MeshPacket makeUndecodableFloodedDm(NodeNum from, PacketId id)
{
    meshtastic_MeshPacket p = makeDupePacket(from, id);
    p.to = 0x12345678; // a specific node, not NODENUM_BROADCAST
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    return p;
}

void test_undecodable_flooded_packet_gets_text_message_tolerance(void)
{
    // An opaque DM flooded with NO_NEXT_HOP_PREFERENCE (no directed route known yet) - treated the
    // same as a decoded text message.
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

void test_undecodable_directed_packet_keeps_default_threshold(void)
{
    // A specific next_hop byte means a directed route is already known - delivery is backed by
    // the sender's end-to-end ACK/retry, so no extra tolerance.
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.next_hop = 0x42;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_undecodable_flooded_broadcast_keeps_default_threshold(void)
{
    // A broadcast we can't decode (e.g. another channel's position or telemetry) is not a DM, so no
    // extra tolerance even though it floods with NO_NEXT_HOP_PREFERENCE.
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.to = NODENUM_BROADCAST;
    p.next_hop = NO_NEXT_HOP_PREFERENCE;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_next_hop_gate_is_overridden_by_a_cached_portnum(void)
{
    // A cache hit (see noteScheduled()) always takes priority over the next_hop gate, even for a
    // flood-relayed packet whose real, known portnum doesn't warrant extra tolerance.
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_POSITION_APP);
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_next_hop_gate_suppressed_by_busy_channel_util(void)
{
    // Same busy-mesh override that applies to the portnum table also applies to the next_hop gate.
    ScopedChannelUtil busy(11.0f); // above the 10% BUSY_CHANNEL_UTIL_PERCENT gate
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_next_hop_gate_tolerated_when_channel_util_low(void)
{
    ScopedChannelUtil notBusy(10.0f); // exactly at the threshold, not above it
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}

#if HAS_VARIABLE_HOPS
void test_next_hop_gate_suppressed_by_busy_direct_node_count(void)
{
    ScopedDirectActiveNodes busy(11); // above the 10-node BUSY_DIRECT_ACTIVE_NODES gate
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
}

void test_next_hop_gate_tolerated_when_direct_node_count_low(void)
{
    ScopedDirectActiveNodes notBusy(10); // exactly at the threshold, not above it
    meshtastic_MeshPacket p = makeUndecodableFloodedDm(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
}
#endif

// Group 3 - meshTooBusyForExtraRepeats gate (channel/air util, direct-neighbor density)

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

// --- the policy itself, called directly: the only way to reach the air-util arm ---

void test_meshTooBusy_quiet_mesh_is_not_busy(void)
{
    TEST_ASSERT_FALSE(RepeatScalingModule::meshTooBusy(0.0f, 0.0f, 0));
}

void test_meshTooBusy_channel_util_boundary(void)
{
    const float gate = RepeatScalingModule::BUSY_CHANNEL_UTIL_PERCENT;
    TEST_ASSERT_FALSE(RepeatScalingModule::meshTooBusy(gate, 0.0f, 0)); // at the gate, not above it
    TEST_ASSERT_TRUE(RepeatScalingModule::meshTooBusy(gate + 1.0f, 0.0f, 0));
}

void test_meshTooBusy_air_util_tx_boundary(void)
{
    const float gate = RepeatScalingModule::BUSY_AIR_UTIL_TX_PERCENT;
    TEST_ASSERT_FALSE(RepeatScalingModule::meshTooBusy(0.0f, gate, 0)); // at the gate, not above it
    TEST_ASSERT_TRUE(RepeatScalingModule::meshTooBusy(0.0f, gate + 1.0f, 0));
}

#if HAS_VARIABLE_HOPS
void test_meshTooBusy_direct_nodes_boundary(void)
{
    const uint16_t gate = RepeatScalingModule::BUSY_DIRECT_ACTIVE_NODES;
    TEST_ASSERT_FALSE(RepeatScalingModule::meshTooBusy(0.0f, 0.0f, gate)); // at the gate, not above it
    TEST_ASSERT_TRUE(RepeatScalingModule::meshTooBusy(0.0f, 0.0f, gate + 1));
}
#endif

// --- and that getDupeCancelThreshold actually consults the (substitutable) gate ---

void test_busy_gate_suppresses_text_message_threshold(void)
{
    RepeatScalingModuleBusyShim busyShim;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    busyShim.testMeshBusy = false;
    TEST_ASSERT_EQUAL_UINT8(2, busyShim.getDupeCancelThreshold(&p));
    busyShim.testMeshBusy = true;
    TEST_ASSERT_EQUAL_UINT8(1, busyShim.getDupeCancelThreshold(&p));
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

// perHop[0] is a 1-in-denominator hash subsample: six sampled at denominator 2 estimate twelve neighbours.
void test_sampled_direct_node_count_is_scaled_by_its_denominator(void)
{
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    {
        ScopedDirectActiveNodes dense(6, 2);
        TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&p));
    }
    {
        ScopedDirectActiveNodes atGate(5, 2); // estimates exactly ten
        TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&p));
    }
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

// Group 4 - shouldCancelDupe (integration: threshold gates the cancel + clears tracking)

void test_shouldCancelDupe_clears_tracking_once_threshold_reached(void)
{
    shim->testThreshold = 1; // historical behavior: give up (and clear) on the very first duplicate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP); // an entry for the cancel to free

    TEST_ASSERT_TRUE(shim->shouldCancelDupe(&p));

    // Threshold 1 cancels at once and frees the packet's entry, cached portnum included.
    TEST_ASSERT_EQUAL_INT32(-1, shim->lookupNotedPortnum(kSender1, kId1));
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

// Group 5 - encrypted duplicates classified only through noteScheduled(), as on air

void test_encrypted_dupe_uses_portnum_noted_when_we_scheduled_our_own_rebroadcast(void)
{
    // Simulates NextHopRouter::perhapsRebroadcast having decoded and scheduled our own copy.
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    // The duplicate heard over the air is still encrypted - this is the packet shape
    // shouldCancelDupe actually sees in production. Broadcast-addressed (makeDupePacket's default),
    // so the next_hop gate alone would give 1 and only the cache can yield 2.
    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    TEST_ASSERT_TRUE(dupe.which_payload_variant == meshtastic_MeshPacket_encrypted_tag);

    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dupe));
}

void test_encrypted_dm_dupe_uses_portnum_noted_when_we_scheduled_our_own_rebroadcast(void)
{
    // Same as the broadcast case above, but addressed as a DM (see the module-level comment in
    // RepeatScalingModule.cpp: NextHopRouter::shouldFilterReceived's dupe path reuses the same
    // inherited FloodingRouter::perhapsCancelDupe -> shouldCancelDupe call, so a DM's encrypted
    // duplicate must get the same noteScheduled-cached tolerance a broadcast's does).
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    dupe.to = 0x12345678; // a specific node, not NODENUM_BROADCAST
    dupe.next_hop = 0x42; // directed, so the next_hop gate alone would give 1
    TEST_ASSERT_TRUE(dupe.which_payload_variant == meshtastic_MeshPacket_encrypted_tag);

    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dupe));
}

void test_encrypted_dupe_without_noteScheduled_falls_back_to_next_hop_gate(void)
{
    // We never scheduled our own rebroadcast of this (sender, id) - e.g. we're not the assigned
    // next hop, or we never decoded it - so there is no cached portnum at all. This is the
    // pre-fix behavior for every encrypted duplicate; now it falls to the next_hop gate (Group 2b)
    // instead of unconditionally defaulting to 1 - pin next_hop explicitly so this test isolates
    // "no cache" from that gate's own behavior.
    meshtastic_MeshPacket dupe = makeDupePacket(kSender2, kId2);
    dupe.next_hop = 0x42; // a specific, directed relay byte - not NO_NEXT_HOP_PREFERENCE
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&dupe));
}

void test_noteScheduled_is_keyed_per_sender_and_id(void)
{
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    // A different (sender, id) pair must not pick up kSender1/kId1's noted portnum. Pin next_hop
    // so the lack of a cache hit (not the next_hop gate) is what's under test here.
    meshtastic_MeshPacket unrelated = makeDupePacket(kSender2, kId2);
    unrelated.next_hop = 0x42;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&unrelated));

    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dupe));
}

void test_noteScheduled_updates_existing_entry(void)
{
    // registerDupeHeard may have already claimed a ring slot for this (sender, id) - e.g. we
    // heard a duplicate before deciding to (re)schedule our own rebroadcast of it (hop-limit
    // upgrade). noteScheduled must update that slot's portnum in place, not just no-op.
    plainShim->registerDupeHeard(kSender1, kId1);
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dupe));
}

void test_clearDupeCount_also_forgets_noted_portnum(void)
{
    plainShim->noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);
    plainShim->clearDupeCount(kSender1, kId1);

    // Once cleared (e.g. shouldCancelDupe already gave up on this packet), a stale portnum must
    // not leak into whatever unrelated packet reuses this ring slot next. Pin next_hop so the
    // cleared cache (not the next_hop gate) is what's under test here.
    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    dupe.next_hop = 0x42;
    TEST_ASSERT_EQUAL_UINT8(1, plainShim->getDupeCancelThreshold(&dupe));
}

static meshtastic_MeshPacket makeScheduledPacket(NodeNum from, PacketId id, meshtastic_PortNum portnum)
{
    meshtastic_MeshPacket p = makeDupePacket(from, id);
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = portnum;
    return p;
}

// A threshold-1 broadcast's cache miss already gives 1, so it must not take a slot.
void test_noteScheduled_packet_skips_threshold_one_broadcasts(void)
{
    RepeatScalingModuleTestShim ring;
    meshtastic_MeshPacket position = makeScheduledPacket(kSender1, kId1, meshtastic_PortNum_POSITION_APP);
    ring.noteScheduled(&position);
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, kId1));
}

// An undecoded packet has no portnum to cache; a miss falls to the same header guess.
void test_noteScheduled_packet_skips_undecoded(void)
{
    RepeatScalingModuleTestShim ring;
    meshtastic_MeshPacket opaque = makeDupePacket(kSender1, kId1);
    opaque.to = 0x12345678;
    ring.noteScheduled(&opaque);
    TEST_ASSERT_EQUAL_INT32(-1, ring.lookupNotedPortnum(kSender1, kId1));
}

// Text broadcasts need the cache for threshold 2; a non-text DM needs it to override the flooded-DM guess.
void test_noteScheduled_packet_caches_text_and_dms(void)
{
    RepeatScalingModuleTestShim ring;
    meshtastic_MeshPacket text = makeScheduledPacket(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);
    meshtastic_MeshPacket dm = makeScheduledPacket(kSender2, kId2, meshtastic_PortNum_POSITION_APP);
    dm.to = 0x12345678;
    ring.noteScheduled(&text);
    ring.noteScheduled(&dm);
    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender1, kId1));
    TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_POSITION_APP, ring.lookupNotedPortnum(kSender2, kId2));
}

// A full TX queue of text plus any number of telemetry relays must not evict a queued text packet's portnum.
void test_noteScheduled_packet_bulk_broadcasts_do_not_evict_queued_text(void)
{
    RepeatScalingModuleTestShim ring;
    for (uint32_t i = 0; i < MAX_TX_QUEUE; i++) {
        meshtastic_MeshPacket text = makeScheduledPacket(kSender1, 0x70000 + i, meshtastic_PortNum_TEXT_MESSAGE_APP);
        ring.noteScheduled(&text);
    }
    for (uint32_t i = 0; i < 50; i++) {
        meshtastic_MeshPacket telemetry = makeScheduledPacket(kSender2, 0x80000 + i, meshtastic_PortNum_TELEMETRY_APP);
        ring.noteScheduled(&telemetry);
    }
    for (uint32_t i = 0; i < MAX_TX_QUEUE; i++)
        TEST_ASSERT_EQUAL_INT32(meshtastic_PortNum_TEXT_MESSAGE_APP, ring.lookupNotedPortnum(kSender1, 0x70000 + i));
}

void test_shouldCancelDupe_end_to_end_with_encrypted_duplicate(void)
{
    // Full path through the real (non-overridden) threshold logic, starting from an encrypted
    // duplicate: this is the regression test for the "portnum=-1" bug. Before the noteScheduled
    // cache existed, this packet always fell back to threshold 1 and cancelled on the very first
    // heard duplicate, regardless of portnum.
    RepeatScalingModulePlainShim endToEnd;
    endToEnd.noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    TEST_ASSERT_FALSE(endToEnd.shouldCancelDupe(&dupe)); // 1st duplicate: tolerated, threshold is 2
    TEST_ASSERT_TRUE(endToEnd.shouldCancelDupe(&dupe));  // 2nd duplicate: threshold reached, cancel
}

void test_shouldCancelDupe_end_to_end_with_encrypted_dm_duplicate(void)
{
    // Same end-to-end regression test as above, but for a DM (to a specific node rather than
    // NODENUM_BROADCAST). Confirms the noteScheduled cache extends the same tolerance to a DM's
    // encrypted duplicate that it does to a broadcast's - matching how NextHopRouter routes both
    // through the same inherited shouldCancelDupe call with no separate addressing-based gate.
    RepeatScalingModulePlainShim endToEnd;
    endToEnd.noteScheduled(kSender1, kId1, meshtastic_PortNum_TEXT_MESSAGE_APP);

    meshtastic_MeshPacket dupe = makeDupePacket(kSender1, kId1);
    dupe.to = 0x12345678;                                // a specific node, not NODENUM_BROADCAST
    dupe.next_hop = 0x42;                                // directed, so only the cache can yield 2
    TEST_ASSERT_FALSE(endToEnd.shouldCancelDupe(&dupe)); // 1st duplicate: tolerated, threshold is 2
    TEST_ASSERT_TRUE(endToEnd.shouldCancelDupe(&dupe));  // 2nd duplicate: threshold reached, cancel
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    suiteAirTime = airTime;
#if HAS_VARIABLE_HOPS
    suiteHopScalingModule = hopScalingModule;
#endif
    shim = new RepeatScalingModuleTestShim();
    plainShim = new RepeatScalingModulePlainShim();

    printf("\n=== registerDupeHeard / clearDupeCount (ring buffer) ===\n");
    RUN_TEST(test_registerDupeHeard_starts_at_one);
    RUN_TEST(test_registerDupeHeard_increments_on_repeat);
    RUN_TEST(test_registerDupeHeard_independent_per_packet);
    RUN_TEST(test_clearDupeCount_resets_tracking);
    RUN_TEST(test_clearDupeCount_of_untracked_packet_is_noop);
    RUN_TEST(test_registerDupeHeard_ring_eviction_does_not_crash);
    RUN_TEST(test_claim_reuses_a_cleared_slot_before_evicting_a_live_one);
    RUN_TEST(test_threshold_one_duplicates_do_not_claim_ring_slots);
    RUN_TEST(test_full_ring_evicts_the_oldest_claim_after_reuse);
    RUN_TEST(test_noteScheduled_on_existing_entry_refreshes_its_claim);

    printf("\n=== getDupeCancelThreshold (compile-time default) ===\n");
    RUN_TEST(test_default_threshold_is_one_for_undecoded_directed_packet);
    RUN_TEST(test_default_threshold_is_one_for_unlisted_portnum);
    RUN_TEST(test_text_message_threshold_is_two);
    RUN_TEST(test_text_message_compressed_threshold_is_two);
    RUN_TEST(test_text_message_threshold_applies_regardless_of_addressing);

    printf("\n=== next_hop gate (undecodable packets, e.g. a relayed PKI DM) ===\n");
    RUN_TEST(test_undecodable_flooded_packet_gets_text_message_tolerance);
    RUN_TEST(test_undecodable_directed_packet_keeps_default_threshold);
    RUN_TEST(test_undecodable_flooded_broadcast_keeps_default_threshold);
    RUN_TEST(test_next_hop_gate_is_overridden_by_a_cached_portnum);
    RUN_TEST(test_next_hop_gate_suppressed_by_busy_channel_util);
    RUN_TEST(test_next_hop_gate_tolerated_when_channel_util_low);
#if HAS_VARIABLE_HOPS
    RUN_TEST(test_next_hop_gate_suppressed_by_busy_direct_node_count);
    RUN_TEST(test_next_hop_gate_tolerated_when_direct_node_count_low);
#endif

    printf("\n=== meshTooBusyForExtraRepeats gate ===\n");
    RUN_TEST(test_busy_channel_util_suppresses_text_message_threshold);
    RUN_TEST(test_channel_util_at_gate_does_not_suppress);
    RUN_TEST(test_meshTooBusy_quiet_mesh_is_not_busy);
    RUN_TEST(test_meshTooBusy_channel_util_boundary);
    RUN_TEST(test_meshTooBusy_air_util_tx_boundary);
#if HAS_VARIABLE_HOPS
    RUN_TEST(test_meshTooBusy_direct_nodes_boundary);
#endif
    RUN_TEST(test_busy_gate_suppresses_text_message_threshold);
#if HAS_VARIABLE_HOPS
    RUN_TEST(test_busy_direct_node_count_suppresses_text_message_threshold);
    RUN_TEST(test_direct_node_count_at_gate_does_not_suppress);
    RUN_TEST(test_sampled_direct_node_count_is_scaled_by_its_denominator);
#endif
    RUN_TEST(test_busy_mesh_does_not_affect_already_unextended_portnums);

    printf("\n=== shouldCancelDupe (threshold gating) ===\n");
    RUN_TEST(test_shouldCancelDupe_clears_tracking_once_threshold_reached);
    RUN_TEST(test_shouldCancelDupe_waits_for_configured_repeats);

    printf("\n=== noteScheduled + encrypted duplicates (as heard for real over the air) ===\n");
    RUN_TEST(test_encrypted_dupe_uses_portnum_noted_when_we_scheduled_our_own_rebroadcast);
    RUN_TEST(test_encrypted_dm_dupe_uses_portnum_noted_when_we_scheduled_our_own_rebroadcast);
    RUN_TEST(test_encrypted_dupe_without_noteScheduled_falls_back_to_next_hop_gate);
    RUN_TEST(test_noteScheduled_is_keyed_per_sender_and_id);
    RUN_TEST(test_noteScheduled_updates_existing_entry);
    RUN_TEST(test_clearDupeCount_also_forgets_noted_portnum);
    RUN_TEST(test_noteScheduled_packet_skips_threshold_one_broadcasts);
    RUN_TEST(test_noteScheduled_packet_skips_undecoded);
    RUN_TEST(test_noteScheduled_packet_caches_text_and_dms);
    RUN_TEST(test_noteScheduled_packet_bulk_broadcasts_do_not_evict_queued_text);
    RUN_TEST(test_shouldCancelDupe_end_to_end_with_encrypted_duplicate);
    RUN_TEST(test_shouldCancelDupe_end_to_end_with_encrypted_dm_duplicate);

    exit(UNITY_END());
}

void loop() {}
