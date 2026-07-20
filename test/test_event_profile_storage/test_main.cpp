#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include <unity.h>

void test_standard_profile_uses_standard_files()
{
    const auto paths = radioProfileStoragePaths(false);

    TEST_ASSERT_EQUAL_STRING("/prefs/config.proto", paths.config);
    TEST_ASSERT_EQUAL_STRING("/prefs/channels.proto", paths.channels);
    TEST_ASSERT_EQUAL_STRING("/backups/backup.proto", paths.backup);
}

void test_event_profile_uses_isolated_files()
{
    const auto paths = radioProfileStoragePaths(true);

    TEST_ASSERT_EQUAL_STRING("/prefs/event-config.proto", paths.config);
    TEST_ASSERT_EQUAL_STRING("/prefs/event-channels.proto", paths.channels);
    TEST_ASSERT_EQUAL_STRING("/backups/event-backup.proto", paths.backup);
}

void test_event_config_preserves_identity_but_replaces_lora()
{
    meshtastic_LocalConfig standard = meshtastic_LocalConfig_init_zero;
    standard.has_security = true;
    standard.security.private_key.size = 1;
    standard.security.private_key.bytes[0] = 0xA5;
    standard.has_device = true;
    standard.device.node_info_broadcast_secs = 600;
    standard.has_lora = true;
    standard.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    standard.lora.channel_num = 20;

    auto eventLora = standard.lora;
    eventLora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    eventLora.channel_num = 3;

    const auto eventConfig = eventConfigFromStandard(standard, eventLora);

    TEST_ASSERT_EQUAL_UINT8(1, eventConfig.security.private_key.size);
    TEST_ASSERT_EQUAL_HEX8(0xA5, eventConfig.security.private_key.bytes[0]);
    TEST_ASSERT_EQUAL_UINT32(600, eventConfig.device.node_info_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(eventLora.region), static_cast<uint32_t>(eventConfig.lora.region));
    TEST_ASSERT_EQUAL_UINT32(3, eventConfig.lora.channel_num);
}

void test_boot_defers_persistence_until_config_is_verified()
{
    TEST_ASSERT_TRUE(shouldDeferBootPersistence(true, false, false));
    TEST_ASSERT_TRUE(shouldDeferBootPersistence(true, true, true));
    TEST_ASSERT_FALSE(shouldDeferBootPersistence(true, true, false));
    TEST_ASSERT_FALSE(shouldDeferBootPersistence(false, true, true));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_standard_profile_uses_standard_files);
    RUN_TEST(test_event_profile_uses_isolated_files);
    RUN_TEST(test_event_config_preserves_identity_but_replaces_lora);
    RUN_TEST(test_boot_defers_persistence_until_config_is_verified);
    exit(UNITY_END());
}

void loop() {}
}
