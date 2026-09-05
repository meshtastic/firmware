#include "Arduino.h"
#include "TestUtil.h"
#include "buzz/BuzzerMode.h"
#include <unity.h>

void test_system_tones_follow_mode_policy()
{
    TEST_ASSERT_TRUE(buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED));
    TEST_ASSERT_FALSE(buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED));
    TEST_ASSERT_FALSE(buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode_NOTIFICATIONS_ONLY));
    TEST_ASSERT_TRUE(buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY));
    TEST_ASSERT_FALSE(buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY));
}

void test_channel_notifications_follow_mode_policy()
{
    TEST_ASSERT_TRUE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED, false));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED, false));
    TEST_ASSERT_TRUE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_NOTIFICATIONS_ONLY, false));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY, false));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY, false));
}

void test_direct_notifications_follow_mode_policy()
{
    TEST_ASSERT_TRUE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED, true));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED, true));
    TEST_ASSERT_TRUE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_NOTIFICATIONS_ONLY, true));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY, true));
    TEST_ASSERT_TRUE(buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY, true));
}

void test_unknown_mode_is_silent()
{
    const auto unknown = static_cast<meshtastic_Config_DeviceConfig_BuzzerMode>(99);
    TEST_ASSERT_FALSE(buzzerModeAllowsSystemTones(unknown));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(unknown, false));
    TEST_ASSERT_FALSE(buzzerModeAllowsNotification(unknown, true));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_system_tones_follow_mode_policy);
    RUN_TEST(test_channel_notifications_follow_mode_policy);
    RUN_TEST(test_direct_notifications_follow_mode_policy);
    RUN_TEST(test_unknown_mode_is_silent);
    exit(UNITY_END());
}

void loop() {}
