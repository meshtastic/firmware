// Unit tests for waypointIsActive() and its caller WaypointStore::isExpired(): the expire == 0 and
// expire == 1 sentinels, ordinary expiry, and an untrusted clock.
#include "TestUtil.h"
#include "WaypointStore.h"
#include "meshUtils.h"
#include <pb_encode.h>
#include <unity.h>

namespace
{
constexpr uint32_t NOW = 1767225600; // 2026-01-01
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_zero_never_expires()
{
    TEST_ASSERT_TRUE(waypointIsActive(0, NOW));
}

void test_one_is_the_delete_convention()
{
    TEST_ASSERT_FALSE(waypointIsActive(1, NOW));
}

void test_future_expiry_is_active()
{
    TEST_ASSERT_TRUE(waypointIsActive(NOW + 3600, NOW));
}

void test_past_expiry_is_not_active()
{
    TEST_ASSERT_FALSE(waypointIsActive(NOW - 1, NOW));
}

void test_expiry_exactly_now_is_not_active()
{
    TEST_ASSERT_FALSE(waypointIsActive(NOW, NOW));
}

void test_int32_max_is_active()
{
    // What Android sends when its expiry switch is off; it is 2038, so it must simply compare future.
    TEST_ASSERT_TRUE(waypointIsActive(INT32_MAX, NOW));
}

void test_untrusted_clock_expires_nothing()
{
    TEST_ASSERT_TRUE(waypointIsActive(NOW - 3600, 0));
    TEST_ASSERT_TRUE(waypointIsActive(NOW + 3600, 0));
    TEST_ASSERT_TRUE(waypointIsActive(0, 0));
}

void test_untrusted_clock_still_honours_delete()
{
    TEST_ASSERT_FALSE(waypointIsActive(1, 0));
}

// The production caller: an explicit now keeps these off the wall clock.
void test_store_expiry_matches_the_predicate()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;

    wp.expire = 0;
    TEST_ASSERT_FALSE(WaypointStore::isExpired(wp, NOW));
    wp.expire = 1;
    TEST_ASSERT_TRUE(WaypointStore::isExpired(wp, NOW));
    wp.expire = NOW + 3600;
    TEST_ASSERT_FALSE(WaypointStore::isExpired(wp, NOW));
    wp.expire = NOW - 1;
    TEST_ASSERT_TRUE(WaypointStore::isExpired(wp, NOW));
}

// rx_time carries uptime rather than an epoch when has_rx_time is false (Router::computeRxTimeStamp),
// so an already-expired waypoint must still be rejected rather than compared against seconds of uptime.
void test_packet_without_rx_time_still_expires()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.id = 4242;
    wp.expire = 1600000000; // September 2020

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = 0x11223344;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.payload.size = (uint16_t)pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                               &meshtastic_Waypoint_msg, &wp);
    packet.has_rx_time = false;
    packet.rx_time = 300; // uptime seconds, not an epoch

    waypointStore.clearAllWaypoints();
    TEST_ASSERT_TRUE(waypointStore.addFromPacket(packet, false));
    TEST_ASSERT_NULL(waypointStore.findWaypoint(wp.id));
    waypointStore.clearAllWaypoints();
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_zero_never_expires);
    RUN_TEST(test_one_is_the_delete_convention);
    RUN_TEST(test_future_expiry_is_active);
    RUN_TEST(test_past_expiry_is_not_active);
    RUN_TEST(test_expiry_exactly_now_is_not_active);
    RUN_TEST(test_int32_max_is_active);
    RUN_TEST(test_untrusted_clock_expires_nothing);
    RUN_TEST(test_untrusted_clock_still_honours_delete);
    RUN_TEST(test_store_expiry_matches_the_predicate);
    RUN_TEST(test_packet_without_rx_time_still_expires);
    exit(UNITY_END());
}

void loop() {}
