// Unit tests for Default::getConfiguredOrDefaultMsScaled
#include "Default.h"
#include "TestUtil.h"
#include <unity.h>

void test_router_no_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 100);
    TEST_ASSERT_EQUAL_UINT32(60000U, res);
}

void test_client_below_threshold()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 40);
    TEST_ASSERT_EQUAL_UINT32(60000U, res);
}

void test_client_default_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    // nodesOverForty = 10 -> coefficient = 1 + 10 * 0.075 = 1.75 => 60000 * 1.75 = 105000
    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 50);
    TEST_ASSERT_EQUAL_UINT32(105000U, res);
}

void test_client_medium_fast_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    // nodesOverForty = 30 -> coefficient = 1 + 30 * 0.02 = 1.6 => 60000 * 1.6 = 96000
    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 70);
    TEST_ASSERT_EQUAL_UINT32(96000U, res);
}

void setup()
{
    // Small delay to match other test mains
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_router_no_scaling);
    RUN_TEST(test_client_below_threshold);
    RUN_TEST(test_client_default_preset_scaling);
    RUN_TEST(test_client_medium_fast_preset_scaling);
    exit(UNITY_END());
}

void loop() {}
