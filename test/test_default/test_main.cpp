// Unit tests for Default::getConfiguredOrDefaultMsScaled
#include "Default.h"
#include "MeshRadio.h"
#include "TestUtil.h"
#include "meshUtils.h"
#include "modules/RoutingModule.h"
#include <algorithm>
#include <unity.h>

// Expected values are worked by hand from Default::congestionScalingCoefficient() (src/mesh/Default.h),
// not recomputed by a copy of it: coeff = 1 + (nodes - 40) * 2^SF / (BW_kHz * 100) above 40 nodes, with 25 in
// place of 100 under USERPREFS_EVENT_MODE, so each scaled case carries both expectations.
// src truncates base * coeff, and the float coefficient is not exact, so scaled results allow +-1 ms.

void test_router_no_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9;
    config.lora.bandwidth = 250;

    TEST_ASSERT_EQUAL_UINT32(60000U, Default::getConfiguredOrDefaultMsScaled(0, 60, 100));
}

void test_client_below_threshold()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9;
    config.lora.bandwidth = 250;

    TEST_ASSERT_EQUAL_UINT32(60000U, Default::getConfiguredOrDefaultMsScaled(0, 60, 40));
}

void test_client_default_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9; // SF9
    config.lora.bandwidth = 250;   // 250 kHz

#if USERPREFS_EVENT_MODE
    // 10 nodes over 40: 60000 * (1 + 10 * 512 / 6250) = 60000 * 1.8192 = 109152
    TEST_ASSERT_UINT32_WITHIN(1, 109152U, Default::getConfiguredOrDefaultMsScaled(0, 60, 50));
#else
    // 10 nodes over 40: 60000 * (1 + 10 * 512 / 25000) = 60000 * 1.2048 = 72288
    TEST_ASSERT_UINT32_WITHIN(1, 72288U, Default::getConfiguredOrDefaultMsScaled(0, 60, 50));
#endif
}

void test_client_medium_fast_preset_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    // Decoys: under use_preset the SF/BW must come from the preset (SF9/250 kHz), not these fields.
    config.lora.spread_factor = 12;
    config.lora.bandwidth = 125;

#if USERPREFS_EVENT_MODE
    // 30 nodes over 40: 60000 * (1 + 30 * 512 / 6250) = 60000 * 3.4576 = 207456
    TEST_ASSERT_UINT32_WITHIN(1, 207456U, Default::getConfiguredOrDefaultMsScaled(0, 60, 70));
#else
    // 30 nodes over 40: 60000 * (1 + 30 * 512 / 25000) = 60000 * 1.6144 = 96864
    TEST_ASSERT_UINT32_WITHIN(1, 96864U, Default::getConfiguredOrDefaultMsScaled(0, 60, 70));
#endif
}

void test_tak_tracker_no_scaling()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
    config.lora.use_preset = false;
    config.lora.spread_factor = 9;
    config.lora.bandwidth = 250;

    TEST_ASSERT_EQUAL_UINT32(60000U, Default::getConfiguredOrDefaultMsScaled(0, 60, 100));
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
    // INT32_MAX / 1000 = 2,147,483 - largest secs that does not clamp.
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

void test_configured_or_default_hop_limit()
{
    config.lora.hop_limit = HOP_MAX;
    const uint8_t result = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);

#if USERPREFS_EVENT_MODE
    TEST_ASSERT_EQUAL_UINT8(Default::eventModeHopLimit, result);
    TEST_ASSERT_EQUAL_UINT8(Default::eventModeHopLimit, Default::getConfiguredOrDefaultHopLimit(Default::eventModeHopLimit));
#else
    TEST_ASSERT_EQUAL_UINT8(HOP_MAX, result);
#endif
}

#if USERPREFS_EVENT_MODE
void test_event_mode_caps_optimized_response()
{
    config.lora.hop_limit = HOP_MAX;
    meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
    request.hop_start = HOP_MAX;
    request.hop_limit = HOP_MAX - 4;

    RoutingModule module;
    TEST_ASSERT_EQUAL_UINT8(std::min<uint8_t>(6, Default::eventModeHopLimit), module.getHopLimitForResponse(request));
}
#endif

// -----------------------------------------------------------------------
// getConfiguredOrDefaultMsScaled(..., TrafficType) - the region-throttle overload
// -----------------------------------------------------------------------
//
// This is the overload every telemetry and position module actually calls, and nothing covered it:
// not the throttle multiply, not the <= 1 short-circuit, not the no-region guard, not the 64-bit
// overflow clamp. Region throttles are real - EU_866 carries PROFILE_LITE with a x10 on both
// position and telemetry - so a change here silently changes broadcast spacing in that region.
//
// Each test pins numOnlineNodes at or below the congestion threshold and uses a non-scaling role,
// so the congestion coefficient is 1 and the only variable left is the throttle.

static const uint32_t kUnscaledNodes = 40; // at/below the threshold: congestion coefficient is 1

static void useRegion(meshtastic_Config_LoRaConfig_RegionCode region)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER; // routers never congestion-scale
    config.lora.region = region;
    initRegion();
}

void test_trafficType_noRegion_returnsUnthrottled()
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    const RegionInfo *saved = myRegion;
    myRegion = nullptr;

    const uint32_t base = Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes);
    TEST_ASSERT_EQUAL_UINT32(base, Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes, TrafficType::TELEMETRY));
    myRegion = saved;
}

void test_trafficType_neutralThrottle_returnsUnthrottled()
{
    // US carries PROFILE_STD, whose position and telemetry throttles are both 1 - the neutral
    // multiplier the implementation short-circuits on.
    useRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL_INT8(1, myRegion->profile->telemetryThrottle);

    const uint32_t base = Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes);
    TEST_ASSERT_EQUAL_UINT32(base, Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes, TrafficType::TELEMETRY));
    TEST_ASSERT_EQUAL_UINT32(base, Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes, TrafficType::POSITION));
}

void test_trafficType_regionThrottleMultiplies()
{
    // EU_866 carries PROFILE_LITE: positionThrottle and telemetryThrottle are both 10.
    useRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_866);
    const int8_t telemetryThrottle = myRegion->profile->telemetryThrottle;
    const int8_t positionThrottle = myRegion->profile->positionThrottle;
    TEST_ASSERT_GREATER_THAN_INT8(1, telemetryThrottle);

    const uint32_t base = Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes);
    TEST_ASSERT_EQUAL_UINT32(base * telemetryThrottle,
                             Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes, TrafficType::TELEMETRY));
    TEST_ASSERT_EQUAL_UINT32(base * positionThrottle,
                             Default::getConfiguredOrDefaultMsScaled(0, 60u, kUnscaledNodes, TrafficType::POSITION));
}

void test_trafficType_overflowSaturates()
{
    // A day-long base times a x10 region throttle exceeds uint32 without the 64-bit guard.
    useRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_866);

    const uint32_t res = Default::getConfiguredOrDefaultMsScaled(0, 3 * ONE_DAY, kUnscaledNodes, TrafficType::TELEMETRY);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(INT32_MAX), res);
}

// Required by Unity: PlatformIO's weak defaults do not link on MinGW (PE-COFF weak externals).
void setUp(void) {}
void tearDown(void) {}

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
    RUN_TEST(test_tak_tracker_no_scaling);
    RUN_TEST(test_router_uses_router_minimums);
    RUN_TEST(test_router_late_uses_router_minimums);
    RUN_TEST(test_client_uses_public_channel_minimums);
    RUN_TEST(test_ms_below_threshold);
    RUN_TEST(test_ms_at_threshold);
    RUN_TEST(test_ms_just_above_threshold);
    RUN_TEST(test_ms_uint32_max);
    RUN_TEST(test_ms_default_clamps);
    RUN_TEST(test_scaled_overflow_saturates);
    RUN_TEST(test_configured_or_default_hop_limit);
    RUN_TEST(test_trafficType_noRegion_returnsUnthrottled);
    RUN_TEST(test_trafficType_neutralThrottle_returnsUnthrottled);
    RUN_TEST(test_trafficType_regionThrottleMultiplies);
    RUN_TEST(test_trafficType_overflowSaturates);
#if USERPREFS_EVENT_MODE
    RUN_TEST(test_event_mode_caps_optimized_response);
#endif
    exit(UNITY_END());
}

void loop() {}
