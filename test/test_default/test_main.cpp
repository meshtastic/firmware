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

    // Routers (including ROUTER_LATE) don't scale
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
        return baseMs;
    }

    // Sensors and trackers don't scale
    if ((config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR) ||
        (config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER)) {
        return baseMs;
    }

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
    // Allow ±1 ms tolerance for floating-point rounding
    TEST_ASSERT_INT_WITHIN(1, expected, res);
}

void test_router_uses_router_minimums()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;

    uint32_t telemetry = Default::getConfiguredOrMinimumValue(60, min_default_telemetry_interval_secs);
    uint32_t position = Default::getConfiguredOrMinimumValue(60, min_default_broadcast_interval_secs);

    TEST_ASSERT_EQUAL_UINT32(ONE_DAY / 2, telemetry);
    TEST_ASSERT_EQUAL_UINT32(ONE_DAY / 2, position);
}

void test_router_late_uses_router_minimums()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;

    uint32_t telemetry = Default::getConfiguredOrMinimumValue(60, min_default_telemetry_interval_secs);
    uint32_t position = Default::getConfiguredOrMinimumValue(60, min_default_broadcast_interval_secs);

    TEST_ASSERT_EQUAL_UINT32(ONE_DAY / 2, telemetry);
    TEST_ASSERT_EQUAL_UINT32(ONE_DAY / 2, position);
}

void test_client_uses_public_channel_minimums()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    uint32_t telemetry = Default::getConfiguredOrMinimumValue(60, min_default_telemetry_interval_secs);
    uint32_t position = Default::getConfiguredOrMinimumValue(60, min_default_broadcast_interval_secs);

    TEST_ASSERT_EQUAL_UINT32(30 * 60, telemetry);
    TEST_ASSERT_EQUAL_UINT32(60 * 60, position);
}

// --- Saturation/clamp tests for getConfiguredOrDefaultMs[Scaled] ---
// These guard the INT32_MAX clamp added to avoid uint32 wrap of secs*1000 and
// to keep results safe to cast to int32_t for OSThread runOnce returns.

void test_ms_below_threshold()
{
    // Ordinary value passes through unchanged.
    TEST_ASSERT_EQUAL_UINT32(60000U, Default::getConfiguredOrDefaultMs(60, 0));
}

void test_ms_at_threshold()
{
    // INT32_MAX / 1000 = 2,147,483 — largest secs that does not clamp.
    TEST_ASSERT_EQUAL_UINT32(2147483000U, Default::getConfiguredOrDefaultMs(2147483U, 0));
}

void test_ms_just_above_threshold()
{
    // One second over the boundary must saturate, not wrap.
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(INT32_MAX), Default::getConfiguredOrDefaultMs(2147484U, 0));
}

void test_ms_uint32_max()
{
    // default_sds_secs == UINT32_MAX on non-routers must not wrap.
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(INT32_MAX), Default::getConfiguredOrDefaultMs(UINT32_MAX, 0));
}

void test_ms_default_clamps()
{
    // Clamp also applies when the default-arg path is taken (configured == 0).
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(INT32_MAX), Default::getConfiguredOrDefaultMs(0, UINT32_MAX));
}

void test_ms_result_is_int32_safe()
{
    // Regression guard for runOnce returns: cast to int32_t must not go negative.
    int32_t result = static_cast<int32_t>(Default::getConfiguredOrDefaultMs(UINT32_MAX, 0));
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, result);
}

void test_scaled_overflow_saturates()
{
    // long_fast (SF11/BW250) with a 24h base and heavy congestion overflows
    // the uint32 result without the double-precision guard. Must saturate.
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    config.lora.spread_factor = 11;
    config.lora.bandwidth = 250;

    uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, ONE_DAY, 1000);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(INT32_MAX), res);
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
    RUN_TEST(test_router_uses_router_minimums);
    RUN_TEST(test_router_late_uses_router_minimums);
    RUN_TEST(test_client_uses_public_channel_minimums);
    RUN_TEST(test_ms_below_threshold);
    RUN_TEST(test_ms_at_threshold);
    RUN_TEST(test_ms_just_above_threshold);
    RUN_TEST(test_ms_uint32_max);
    RUN_TEST(test_ms_default_clamps);
    RUN_TEST(test_ms_result_is_int32_safe);
    RUN_TEST(test_scaled_overflow_saturates);
    exit(UNITY_END());
}

void loop() {}
