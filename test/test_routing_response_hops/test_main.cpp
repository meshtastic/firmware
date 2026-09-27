// RoutingModule::getHopLimitForResponse - the hop budget stamped on every reply/ACK/NAK - and
// MeshModule::setReplyTo() applying it, driven through getHopsAway()'s sentinel rules.

#include "MeshTypes.h" // before TestUtil.h: provides NodeNum etc.
#include "TestUtil.h"
#include <cstdlib> // exit(), needed on both guard branches
#include <unity.h>

// Event mode compiles out the uncapped long-path branch and swaps the configured limit for the
// event hop limit; this suite pins the standard-mode branches only (the event cap is covered by
// test_default's event-mode group).
#if !USERPREFS_EVENT_MODE

#include "configuration.h"
#include "mesh/MeshModule.h"
#include "mesh/NodeDB.h"
#include "modules/RoutingModule.h"
#include <cstdio>

static constexpr NodeNum kRequester = 0x22222222;

static RoutingModule *testRoutingModule = nullptr;

// A received request packet whose hop fields we control. Decoded packets carry the bitfield flag
// that getHopsAway() uses to decide whether hop_start==0 is genuine or a legacy-firmware zero.
static meshtastic_MeshPacket makeRequest(uint8_t hopStart, uint8_t hopLimit, bool decoded = true, bool hasBitfield = true)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = kRequester;
    p.to = 0x11111111;
    p.id = 0xABCD1234;
    p.hop_start = hopStart;
    p.hop_limit = hopLimit;
    if (decoded) {
        p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        p.decoded.has_bitfield = hasBitfield;
    } else {
        p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        p.encrypted.size = 8;
    }
    return p;
}

static meshtastic_MeshPacket makeReply()
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    return p;
}

void setUp(void)
{
    config.lora.hop_limit = 3;
}

void tearDown(void) {}

// ===========================================================================
// Group 1 - unknown hop distance: every unreliable-header shape must fall back
// to the configured limit, never to a value derived from the bogus fields.
// ===========================================================================

void test_encrypted_hop_start_zero_falls_back_to_configured_limit(void)
{
    // Encrypted packet: the bitfield is unreadable, so hop_start==0 cannot be trusted.
    auto request = makeRequest(0, 0, /*decoded=*/false);
    TEST_ASSERT_EQUAL_UINT8(3, testRoutingModule->getHopLimitForResponse(request));
}

void test_decoded_legacy_no_bitfield_falls_back_to_configured_limit(void)
{
    // Pre-2.3.0 senders never populate hop_start and pre-2.5.0 senders never set the bitfield.
    auto request = makeRequest(0, 0, /*decoded=*/true, /*hasBitfield=*/false);
    TEST_ASSERT_EQUAL_UINT8(3, testRoutingModule->getHopLimitForResponse(request));
}

void test_forged_hop_start_below_hop_limit_falls_back_to_configured_limit(void)
{
    // hop_start < hop_limit is impossible for an honest sender; getHopsAway() rejects it.
    auto request = makeRequest(2, 5);
    TEST_ASSERT_EQUAL_UINT8(3, testRoutingModule->getHopLimitForResponse(request));
}

void test_hostile_hop_start_wraps_negative_falls_back_to_configured_limit(void)
{
    // hop_start is 3 bits on the wire but 8 bits via local injection: 255 - 0 narrows to
    // int8_t -1 in getHopsAway(), which lands in the same "unknown" fallback (any
    // hop_start - hop_limit >= 128 reads as negative).
    auto request = makeRequest(255, 0);
    TEST_ASSERT_EQUAL_UINT8(3, testRoutingModule->getHopLimitForResponse(request));
}

// ===========================================================================
// Group 2 - known hop distance: hopsUsed + 2 margin, its clamp boundary, and
// the intentionally uncapped long-path branch.
// ===========================================================================

void test_direct_neighbor_response_gets_two_hop_margin(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(3, 3); // 0 hops used
    TEST_ASSERT_EQUAL_UINT8(2, testRoutingModule->getHopLimitForResponse(request));
}

void test_two_hops_used_gets_margin_of_two(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(3, 1); // 2 hops used
    TEST_ASSERT_EQUAL_UINT8(4, testRoutingModule->getHopLimitForResponse(request));
}

void test_margin_just_below_boundary_still_applies(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(7, 3); // 4 hops used: 4 + 2 = 6 < 7
    TEST_ASSERT_EQUAL_UINT8(6, testRoutingModule->getHopLimitForResponse(request));
}

void test_margin_at_boundary_clamps_to_configured_limit(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(7, 2); // 5 hops used: 5 + 2 == 7, not < 7 -> clamp
    TEST_ASSERT_EQUAL_UINT8(7, testRoutingModule->getHopLimitForResponse(request));
}

void test_hops_equal_to_limit_returns_limit(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(7, 0); // 7 hops used == limit: not "more than", no margin room
    TEST_ASSERT_EQUAL_UINT8(7, testRoutingModule->getHopLimitForResponse(request));
}

void test_long_path_exceeds_configured_limit_uncapped(void)
{
    // Intentional exceed: a request that took more hops than our configured limit gets a
    // response with the same hop count, otherwise the reply dies short of the requester.
    auto request = makeRequest(7, 0); // 7 hops used, configured limit 3
    TEST_ASSERT_EQUAL_UINT8(7, testRoutingModule->getHopLimitForResponse(request));
}

// ===========================================================================
// Group 3 - zero-hop requester
// ===========================================================================

void test_zero_hop_requester_gets_zero_hop_response(void)
{
    // hop_start==0 with the bitfield present is a genuine "0 hops requested": the sender is
    // modern firmware that deliberately sent direct-only, so the response stays local too.
    auto request = makeRequest(0, 0, /*decoded=*/true, /*hasBitfield=*/true);
    TEST_ASSERT_EQUAL_UINT8(0, testRoutingModule->getHopLimitForResponse(request));
}

// ===========================================================================
// Group 4 - configured-limit edges through Default::getConfiguredOrDefaultHopLimit
// ===========================================================================

void test_config_above_hop_max_clamps_to_hop_max(void)
{
    config.lora.hop_limit = 10; // out-of-range config (protobuf allows up to 255)
    auto request = makeRequest(0, 0, /*decoded=*/false);
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, testRoutingModule->getHopLimitForResponse(request));
}

void test_config_zero_yields_zero_for_unknown_hops(void)
{
    // Pins current behavior: getConfiguredOrDefaultHopLimit(0) passes the zero through (no
    // default substitution), so an unknown-distance requester gets a 0-hop response.
    config.lora.hop_limit = 0;
    auto request = makeRequest(0, 0, /*decoded=*/false);
    TEST_ASSERT_EQUAL_UINT8(0, testRoutingModule->getHopLimitForResponse(request));
}

void test_config_zero_known_hops_returns_hops_used(void)
{
    // With a zero configured limit, any known hop count is "more than the limit" and is used
    // as-is - a zero config does not strand replies to multi-hop requesters.
    config.lora.hop_limit = 0;
    auto request = makeRequest(3, 1); // 2 hops used
    TEST_ASSERT_EQUAL_UINT8(2, testRoutingModule->getHopLimitForResponse(request));
}

// ===========================================================================
// Group 5 - setReplyTo() stamps the computed hop limit onto reply packets
// ===========================================================================

void test_setreplyto_stamps_computed_hop_limit_and_reply_fields(void)
{
    config.lora.hop_limit = 7;
    auto request = makeRequest(3, 1); // 2 hops used -> response hop limit 4
    request.channel = 2;
    request.want_ack = true;

    auto reply = makeReply();
    setReplyTo(&reply, request);

    TEST_ASSERT_EQUAL_HEX32(kRequester, reply.to);
    TEST_ASSERT_EQUAL_UINT8(2, reply.channel);
    TEST_ASSERT_EQUAL_UINT8(4, reply.hop_limit);
    TEST_ASSERT_TRUE(reply.want_ack);
    TEST_ASSERT_EQUAL_HEX32(request.id, reply.decoded.request_id);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_Priority_RELIABLE, reply.priority);
}

void test_setreplyto_preserves_existing_priority(void)
{
    auto request = makeRequest(0, 0, /*decoded=*/false); // unknown hops -> configured limit 3
    request.want_ack = false;

    auto reply = makeReply();
    reply.priority = meshtastic_MeshPacket_Priority_ACK;
    setReplyTo(&reply, request);

    TEST_ASSERT_EQUAL_UINT8(3, reply.hop_limit);
    TEST_ASSERT_FALSE(reply.want_ack);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_Priority_ACK, reply.priority);
}

// ===========================================================================

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    testRoutingModule = new RoutingModule();
    routingModule = testRoutingModule; // setReplyTo() reaches the module through the global

    printf("\n=== unknown hop distance falls back to configured limit ===\n");
    RUN_TEST(test_encrypted_hop_start_zero_falls_back_to_configured_limit);
    RUN_TEST(test_decoded_legacy_no_bitfield_falls_back_to_configured_limit);
    RUN_TEST(test_forged_hop_start_below_hop_limit_falls_back_to_configured_limit);
    RUN_TEST(test_hostile_hop_start_wraps_negative_falls_back_to_configured_limit);

    printf("\n=== known hop distance: margin, clamp, uncapped long path ===\n");
    RUN_TEST(test_direct_neighbor_response_gets_two_hop_margin);
    RUN_TEST(test_two_hops_used_gets_margin_of_two);
    RUN_TEST(test_margin_just_below_boundary_still_applies);
    RUN_TEST(test_margin_at_boundary_clamps_to_configured_limit);
    RUN_TEST(test_hops_equal_to_limit_returns_limit);
    RUN_TEST(test_long_path_exceeds_configured_limit_uncapped);

    printf("\n=== zero-hop requester ===\n");
    RUN_TEST(test_zero_hop_requester_gets_zero_hop_response);

    printf("\n=== configured-limit edges ===\n");
    RUN_TEST(test_config_above_hop_max_clamps_to_hop_max);
    RUN_TEST(test_config_zero_yields_zero_for_unknown_hops);
    RUN_TEST(test_config_zero_known_hops_returns_hops_used);

    printf("\n=== setReplyTo integration ===\n");
    RUN_TEST(test_setreplyto_stamps_computed_hop_limit_and_reply_fields);
    RUN_TEST(test_setreplyto_preserves_existing_priority);

    exit(UNITY_END());
}

void loop() {}

#else // USERPREFS_EVENT_MODE

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
