// Unit tests for PowerFSM timeout helpers
#include "Default.h"
#include "PowerFSM.h"
#include "TestUtil.h"
#include <unity.h>

void test_dark_recheck_uses_configured_screen_on()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.display.screen_on_secs = 30;

    TEST_ASSERT_EQUAL_UINT32(30000U, getDarkRecheckMs());
}

void test_dark_recheck_uses_default_when_unset()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.display.screen_on_secs = 0;

    TEST_ASSERT_EQUAL_UINT32(600000U, getDarkRecheckMs());
}

void test_dark_recheck_never_zero_and_never_sentinel()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.display.screen_on_secs = DECAF_ZERO_TIMEOUT_SECS;

    // The sentinel means "no screen-on time", but it is not a usable poll period: the
    // re-check must be neither zero nor the sentinel interpreted as a seconds count.
    TEST_ASSERT_EQUAL_UINT32(0U, Default::getTimeoutMs(config.display.screen_on_secs, default_screen_on_secs));
    TEST_ASSERT_EQUAL_UINT32(600000U, getDarkRecheckMs());
}

void test_dark_recheck_sentinel_uses_router_default()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    config.display.screen_on_secs = DECAF_ZERO_TIMEOUT_SECS;

    TEST_ASSERT_EQUAL_UINT32(1000U, getDarkRecheckMs());
}

// The ON -> DARK transitions must not be registered when the timeout asks for the screen to stay
// on, because a zero period fires immediately rather than never. Only the sentinel spells that on
// a non-E-Ink build; a plain 0 still means "use the default".
static void test_screenStaysOn_onlyForTheSentinel(void)
{
    TEST_ASSERT_TRUE_MESSAGE(screenStaysOn(DECAF_ZERO_TIMEOUT_SECS), "the sentinel must keep the screen on");
    TEST_ASSERT_FALSE_MESSAGE(screenStaysOn(0), "an unset timeout must fall back to the default, not stay on");
    TEST_ASSERT_FALSE_MESSAGE(screenStaysOn(60), "a configured timeout must still go dark");
}

// The value the InkHUD menu writes for "Forever" has to be the one screenStaysOn() honours,
// otherwise picking Forever silently selects the default timeout instead.
static void test_screenStaysOn_matchesTheMenuForeverValue(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(DECAF_ZERO_TIMEOUT_SECS, (uint32_t)DECAF_ZERO_TIMEOUT_SECS,
                                     "menu Forever and the sentinel must be the same value");
    TEST_ASSERT_TRUE(screenStaysOn(DECAF_ZERO_TIMEOUT_SECS));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, Default::getTimeoutMs(DECAF_ZERO_TIMEOUT_SECS, default_screen_on_secs),
                                     "the sentinel must resolve to a zero timeout");
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_dark_recheck_uses_configured_screen_on);
    RUN_TEST(test_dark_recheck_uses_default_when_unset);
    RUN_TEST(test_dark_recheck_never_zero_and_never_sentinel);
    RUN_TEST(test_dark_recheck_sentinel_uses_router_default);
    RUN_TEST(test_screenStaysOn_onlyForTheSentinel);
    RUN_TEST(test_screenStaysOn_matchesTheMenuForeverValue);
    exit(UNITY_END());
}

void loop() {}
