// Unit tests for Default::getConfiguredOrDefaultMsScaled
#include "Default.h"
#include "MeshRadio.h"
#include "TestUtil.h"
#include "meshUtils.h"
#include <unity.h>

// Helper to compute expected ms using same logic as Default::congestionScalingCoefficient
static uint32_t computeExpectedMs(uint32_t defaultSeconds, uint32_t numOnlineNodes)
{
    uint32_t baseMs = Default::getConfiguredOrDefaultMs(0, defaultSeconds);
    if (numOnlineNodes <= 40) {
        return baseMs;
    }

    float bwKHz =
        config.lora.use_preset ? modemPresetToBwKHz(config.lora.modem_preset, false) : bwCodeToKHz(config.lora.bandwidth);

    uint8_t sf = config.lora.spread_factor;
    if (sf < 7)
        sf = 7;
    else if (sf > 12)
        sf = 12;

    float throttlingFactor = static_cast<float>(pow_of_2(sf)) / (bwKHz * 100.0f);
#if USERPREFS_EVENT_MODE
    throttlingFactor = static_cast<float>(pow_of_2(sf)) / (bwKHz * 25.0f);
#endif

    int nodesOverForty = (numOnlineNodes - 40);
    float coeff = 1.0f + (nodesOverForty * throttlingFactor);
    return static_cast<uint32_t>(baseMs * coeff + 0.5f);
}

void test_router_no_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    // set some sane lora config so bootstrap paths are deterministic
    config.lora.use_preset = false;
    config.lora.spread_factor = 9;
    config.lora.bandwidth = 250;

    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 100);
    uint32_t expected = computeExpectedMs(60, 100);
    TEST_ASSERT_EQUAL_UINT32(expected, res);
}

void test_client_below_threshold()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9;
    config.lora.bandwidth = 250;

    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 40);
    uint32_t expected = computeExpectedMs(60, 40);
    TEST_ASSERT_EQUAL_UINT32(expected, res);
}

void test_client_default_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9; // SF9
    config.lora.bandwidth = 250;   // 250 kHz

    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 50);
    uint32_t expected = computeExpectedMs(60, 50); // nodesOverForty = 10
    TEST_ASSERT_EQUAL_UINT32(expected, res);
}

void test_client_medium_fast_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    // nodesOverForty = 30 -> test with nodes=70
    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 60, 70);
    uint32_t expected = computeExpectedMs(60, 70);
    TEST_ASSERT_EQUAL_UINT32(expected, res);
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
