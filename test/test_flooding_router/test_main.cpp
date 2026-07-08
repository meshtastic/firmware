// Unit tests for FloodingRouter's duplicate-rebroadcast bookkeeping:
//   - registerDupeHeard/clearDupeCount: the small ring buffer counting how many times we've
//     heard a duplicate of a packet we have queued to rebroadcast ourselves.
//   - getDupeCancelThreshold: the compile-time, per-portnum threshold (see FloodingRouter.cpp)
//     controlling how many duplicates we tolerate before giving up on our own rebroadcast.
//   - perhapsCancelDupe: the integration of the two - only actually gives up (and clears its
//     tracking state) once the configured threshold has been reached.
//
// perhapsCancelDupe() is safe to call directly here even with no RadioInterface: Router::
// cancelSending() is iface-null-safe (just returns false), so the queue-cancellation side effect
// is a no-op - what we can and do observe is the dupe-count bookkeeping around it.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <unity.h>

#include "configuration.h"
#include "mesh/FloodingRouter.h"

static constexpr NodeNum kSender1 = 0x000005AB;
static constexpr NodeNum kSender2 = 0x000006CD;
static constexpr PacketId kId1 = 0x1001;
static constexpr PacketId kId2 = 0x2002;

// ---------------------------------------------------------------------------
// Test shim - re-exposes the protected dupe-tracking helpers, and allows overriding
// getDupeCancelThreshold so tests don't depend on any real portnum having a non-default case.
// Nulls cryptLock before it's rebuilt so the Router base can be (re)constructed (same pattern as
// test_nexthop_routing's NextHopRouterTestShim / test_mqtt's MockRouter).
// ---------------------------------------------------------------------------
class FloodingRouterTestShim : public FloodingRouter
{
  public:
    FloodingRouterTestShim() : FloodingRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    using FloodingRouter::clearDupeCount;
    using FloodingRouter::getDupeCancelThreshold;
    using FloodingRouter::perhapsCancelDupe;
    using FloodingRouter::registerDupeHeard;

    // Test-only override: bypasses the compile-time portnum switch so tests can exercise
    // perhapsCancelDupe's threshold-gating without needing a real portnum case.
    uint8_t testThreshold = 1;
    uint8_t getDupeCancelThreshold(const meshtastic_MeshPacket *p) override { return testThreshold; }

  protected:
    // FloodingRouter::perhapsRebroadcast is pure virtual; not exercised by these tests.
    bool perhapsRebroadcast(const meshtastic_MeshPacket *p) override { return false; }
};

// A second, minimal shim that does NOT override getDupeCancelThreshold, for testing the real
// compile-time default implementation directly (FloodingRouter itself is abstract - perhapsRebroadcast
// is pure virtual - so it can't be constructed on its own).
class FloodingRouterPlainShim : public FloodingRouter
{
  public:
    FloodingRouterPlainShim() : FloodingRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    using FloodingRouter::getDupeCancelThreshold;

  protected:
    bool perhapsRebroadcast(const meshtastic_MeshPacket *p) override { return false; }
};

static FloodingRouterTestShim *shim = nullptr;
static FloodingRouterPlainShim *plainShim = nullptr;

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
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
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
    // DUPE_COUNT_TRACKER_SIZE is 8 (see FloodingRouter.h); track more distinct packets than that
    // and confirm the ring buffer just evicts older entries rather than misbehaving.
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
    // NextHopRouter::shouldFilterReceived's call to the same inherited perhapsCancelDupe/
    // getDupeCancelThreshold extend the tolerance to DM text messages too, with no separate
    // addressing-based gate.
    meshtastic_MeshPacket dm = makeDupePacket(kSender1, kId1);
    dm.to = 0x12345678; // a specific node, not NODENUM_BROADCAST
    dm.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    dm.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL_UINT8(2, plainShim->getDupeCancelThreshold(&dm));
}

// ===========================================================================
// Group 3 - perhapsCancelDupe (integration: threshold gates the cancel + clears tracking)
// ===========================================================================

void test_perhapsCancelDupe_clears_tracking_once_threshold_reached(void)
{
    shim->testThreshold = 1; // historical behavior: cancel (and clear) on the very first duplicate
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);

    // Threshold (1) was reached on the first call, so tracking was cleared - the next duplicate
    // heard for this packet starts a fresh count, rather than continuing to accumulate.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

void test_perhapsCancelDupe_waits_for_configured_repeats(void)
{
    shim->testThreshold = 3; // this node should tolerate hearing 2 repeats before giving up
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p); // 1st duplicate heard -> internal count 1, below threshold

    // Directly registering one more duplicate both (a) advances the count to 2 for the next
    // step, and (b) proves tracking was NOT cleared by the call above: a cleared entry would
    // restart at 1, not continue to 2.
    TEST_ASSERT_EQUAL_UINT8(2, shim->registerDupeHeard(kSender1, kId1));

    shim->perhapsCancelDupe(&p); // 3rd duplicate heard -> internal count 3, reaches threshold
    // Threshold reached inside the call above, so tracking was cleared - starts fresh at 1.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

void test_perhapsCancelDupe_router_role_never_tracks_or_cancels(void)
{
    // ROUTER never cancels its own rebroadcast (roleAllowsCancelingDupe() gates it out entirely),
    // so it should also never bother tracking a dupe count for it.
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    shim->testThreshold = 1;
    meshtastic_MeshPacket p = makeDupePacket(kSender1, kId1);

    shim->perhapsCancelDupe(&p);
    shim->perhapsCancelDupe(&p);

    // Since roleAllowsCancelingDupe() short-circuits before registerDupeHeard() is ever called,
    // this key was never tracked - the first direct registerDupeHeard() call starts fresh at 1.
    TEST_ASSERT_EQUAL_UINT8(1, shim->registerDupeHeard(kSender1, kId1));
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    shim = new FloodingRouterTestShim();
    plainShim = new FloodingRouterPlainShim();

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

    printf("\n=== perhapsCancelDupe (threshold gating) ===\n");
    RUN_TEST(test_perhapsCancelDupe_clears_tracking_once_threshold_reached);
    RUN_TEST(test_perhapsCancelDupe_waits_for_configured_repeats);
    RUN_TEST(test_perhapsCancelDupe_router_role_never_tracks_or_cancels);

    exit(UNITY_END());
}

void loop() {}
