// Unit tests for RouterRetirementModule's pure policy helpers (src/modules/RouterRetirementModule).
// These are static, globals-free functions — the retirement *decision* logic. The credit-accrual
// + demotion wiring in runOnce()/retireOneRung() touches NodeDB/config/reboot and is integration,
// exercised on the simulator/bench, not here.
#include "Arduino.h"
#include "TestUtil.h"
#include "modules/RouterRetirementModule.h"
#include <unity.h>

#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

using Role = meshtastic_Config_DeviceConfig_Role;
static constexpr Role ROUTER = meshtastic_Config_DeviceConfig_Role_ROUTER;
static constexpr Role ROUTER_LATE = meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
static constexpr Role CLIENT = meshtastic_Config_DeviceConfig_Role_CLIENT;
static constexpr Role CLIENT_BASE = meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
static constexpr uint32_t DEF = RouterRetirementModule::DEFAULT_STEP_THRESHOLD_SECS;

void setUp(void) {}
void tearDown(void) {}

// --- isRetirableRole ---
void test_isRetirableRole()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::isRetirableRole(ROUTER));
    TEST_ASSERT_TRUE(RouterRetirementModule::isRetirableRole(ROUTER_LATE));
    TEST_ASSERT_FALSE(RouterRetirementModule::isRetirableRole(CLIENT));
    TEST_ASSERT_FALSE(RouterRetirementModule::isRetirableRole(CLIENT_BASE));
}

// --- the slope: ROUTER -> ROUTER_LATE -> CLIENT ---
void test_slope_router_to_router_late()
{
    TEST_ASSERT_EQUAL_INT(ROUTER_LATE, RouterRetirementModule::nextRetirementRole(ROUTER));
}
void test_slope_router_late_to_client()
{
    TEST_ASSERT_EQUAL_INT(CLIENT, RouterRetirementModule::nextRetirementRole(ROUTER_LATE));
}
void test_slope_bottom_is_noop()
{
    // CLIENT is the ladder bottom; non-retirable roles return unchanged (no further demotion).
    TEST_ASSERT_EQUAL_INT(CLIENT, RouterRetirementModule::nextRetirementRole(CLIENT));
    TEST_ASSERT_EQUAL_INT(CLIENT_BASE, RouterRetirementModule::nextRetirementRole(CLIENT_BASE));
}

// --- threshold resolution ---
void test_effectiveThreshold_zero_uses_default()
{
    TEST_ASSERT_EQUAL_UINT32(DEF, RouterRetirementModule::effectiveThresholdSecs(0));
}
void test_effectiveThreshold_nonzero_used_verbatim()
{
    TEST_ASSERT_EQUAL_UINT32(1234u, RouterRetirementModule::effectiveThresholdSecs(1234u));
}
void test_default_threshold_is_90_days()
{
    TEST_ASSERT_EQUAL_UINT32(90u * 24 * 60 * 60, DEF); // 7,776,000 s
}

// --- shouldRetire ---
void test_shouldRetire_disabled_never_retires()
{
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(false, ROUTER, DEF + 1, DEF));
}
void test_shouldRetire_router_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(true, ROUTER, DEF, DEF)); // >= boundary
}
void test_shouldRetire_router_below_threshold()
{
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, ROUTER, DEF - 1, DEF));
}
void test_shouldRetire_router_late_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(true, ROUTER_LATE, DEF, DEF));
}
void test_shouldRetire_client_never_retires()
{
    // Non-retirable role: even with enormous credit, never demote.
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, CLIENT, 0xFFFFFFFFu, DEF));
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, CLIENT_BASE, 0xFFFFFFFFu, DEF));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_isRetirableRole);
    RUN_TEST(test_slope_router_to_router_late);
    RUN_TEST(test_slope_router_late_to_client);
    RUN_TEST(test_slope_bottom_is_noop);
    RUN_TEST(test_effectiveThreshold_zero_uses_default);
    RUN_TEST(test_effectiveThreshold_nonzero_used_verbatim);
    RUN_TEST(test_default_threshold_is_90_days);
    RUN_TEST(test_shouldRetire_disabled_never_retires);
    RUN_TEST(test_shouldRetire_router_at_threshold);
    RUN_TEST(test_shouldRetire_router_below_threshold);
    RUN_TEST(test_shouldRetire_router_late_at_threshold);
    RUN_TEST(test_shouldRetire_client_never_retires);
    exit(UNITY_END());
}

void loop() {}

#else // module compiled out — keep the suite linkable/green

void setUp(void) {}
void tearDown(void) {}
void test_excluded_placeholder()
{
    TEST_PASS();
}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_excluded_placeholder);
    exit(UNITY_END());
}
void loop() {}

#endif
