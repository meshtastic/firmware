// Unit tests for waypointIsActive() and its caller WaypointStore::isExpired(): the expire == 0 and
// expire == 1 sentinels, ordinary expiry, and an untrusted clock.
#include "TestUtil.h"
#include "WaypointStore.h"
#include "meshUtils.h"
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
    exit(UNITY_END());
}

void loop() {}
