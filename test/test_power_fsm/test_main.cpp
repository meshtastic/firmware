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

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_dark_recheck_uses_configured_screen_on);
    RUN_TEST(test_dark_recheck_uses_default_when_unset);
    RUN_TEST(test_dark_recheck_never_zero_and_never_sentinel);
    RUN_TEST(test_dark_recheck_sentinel_uses_router_default);
    exit(UNITY_END());
}

void loop() {}
