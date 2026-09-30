// DetectionSensor dwell / burst persistence helpers.
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include "modules/DetectionSensorBurst.h"
#include "modules/DetectionSensorDwell.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_minimum_detect_secs_defaults_zero(void)
{
    meshtastic_ModuleConfig_DetectionSensorConfig cfg = meshtastic_ModuleConfig_DetectionSensorConfig_init_default;
    TEST_ASSERT_EQUAL_UINT32(0, cfg.minimum_detect_secs);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.burst_gap_secs);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.minimum_alert_secs);
    TEST_ASSERT_FALSE(cfg.send_clear);
}

void test_new_fields_roundtrip(void)
{
    meshtastic_ModuleConfig_DetectionSensorConfig cfg = meshtastic_ModuleConfig_DetectionSensorConfig_init_zero;
    cfg.enabled = true;
    cfg.minimum_detect_secs = 1;
    cfg.burst_gap_secs = 3;
    cfg.minimum_alert_secs = 8;
    cfg.send_clear = true;
    cfg.monitor_pin = 21;

    uint8_t buf[64];
    size_t n = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_ModuleConfig_DetectionSensorConfig_msg, &cfg);
    TEST_ASSERT_TRUE(n > 0);

    meshtastic_ModuleConfig_DetectionSensorConfig decoded = meshtastic_ModuleConfig_DetectionSensorConfig_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(buf, n, &meshtastic_ModuleConfig_DetectionSensorConfig_msg, &decoded));
    TEST_ASSERT_EQUAL_UINT32(1, decoded.minimum_detect_secs);
    TEST_ASSERT_EQUAL_UINT32(3, decoded.burst_gap_secs);
    TEST_ASSERT_EQUAL_UINT32(8, decoded.minimum_alert_secs);
    TEST_ASSERT_TRUE(decoded.send_clear);
}

void test_dwell_immediate_when_zero(void)
{
    bool armed = false;
    uint32_t started = 0;
    TEST_ASSERT_TRUE(detectionSensorUpdateDwell(true, 0, 1000, armed, started));
    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(false, 0, 1000, armed, started));
}

void test_dwell_requires_hold_then_confirms(void)
{
    bool armed = false;
    uint32_t started = 0;

    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(true, 2, 1000, armed, started));
    TEST_ASSERT_TRUE(armed);
    TEST_ASSERT_EQUAL_UINT32(1000, started);

    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(true, 2, 2500, armed, started));
    TEST_ASSERT_TRUE(detectionSensorUpdateDwell(true, 2, 3000, armed, started));

    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(false, 2, 3100, armed, started));
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(true, 2, 3200, armed, started));
    TEST_ASSERT_FALSE(detectionSensorUpdateDwell(true, 2, 4000, armed, started));
    TEST_ASSERT_TRUE(detectionSensorUpdateDwell(true, 2, 5200, armed, started));
}

void test_burst_coalesces_retrigger_gaps(void)
{
    DetectionSensorBurstState st;
    // Open burst on first confirm; alert deferred (minimum_alert_secs=5).
    auto r = detectionSensorUpdateBurst(true, true, 1000, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventNone, r.event);
    TEST_ASSERT_TRUE(st.inBurst);

    // Pulse dips low for 1s (< burst_gap 2s) - still same burst.
    r = detectionSensorUpdateBurst(false, false, 2000, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventNone, r.event);
    TEST_ASSERT_TRUE(st.inBurst);

    // High again - quiet cancelled.
    r = detectionSensorUpdateBurst(true, false, 2500, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventNone, r.event);
    TEST_ASSERT_TRUE(st.inBurst);
    TEST_ASSERT_FALSE(st.quietArmed);

    // Persistence reaches 5s → alert once.
    r = detectionSensorUpdateBurst(true, false, 6000, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventAlert, r.event);
    TEST_ASSERT_EQUAL_UINT32(5000, r.burstMs);
    TEST_ASSERT_TRUE(st.alertSent);

    // Quiet for >= gap → single cleared with wall burst_ms.
    r = detectionSensorUpdateBurst(false, false, 7000, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventNone, r.event);
    r = detectionSensorUpdateBurst(false, false, 9000, 2, 5, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventCleared, r.event);
    TEST_ASSERT_EQUAL_UINT32(8000, r.burstMs);
    TEST_ASSERT_FALSE(st.inBurst);
}

void test_burst_alert_immediate_when_threshold_zero(void)
{
    DetectionSensorBurstState st;
    auto r = detectionSensorUpdateBurst(true, true, 1000, 0, 0, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventAlert, r.event);
    r = detectionSensorUpdateBurst(false, false, 2500, 0, 0, 1000, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventCleared, r.event);
    TEST_ASSERT_EQUAL_UINT32(1500, r.burstMs);
}

void test_short_burst_below_alert_threshold_no_alert(void)
{
    DetectionSensorBurstState st;
    auto r = detectionSensorUpdateBurst(true, true, 0, 0, 5, 0, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventNone, r.event);
    r = detectionSensorUpdateBurst(false, false, 2000, 0, 5, 0, st);
    TEST_ASSERT_EQUAL(DetectionSensorBurstEventCleared, r.event);
    TEST_ASSERT_EQUAL_UINT32(2000, r.burstMs);
    TEST_ASSERT_FALSE(st.alertSent);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_minimum_detect_secs_defaults_zero);
    RUN_TEST(test_new_fields_roundtrip);
    RUN_TEST(test_dwell_immediate_when_zero);
    RUN_TEST(test_dwell_requires_hold_then_confirms);
    RUN_TEST(test_burst_coalesces_retrigger_gaps);
    RUN_TEST(test_burst_alert_immediate_when_threshold_zero);
    RUN_TEST(test_short_burst_below_alert_threshold_no_alert);
    exit(UNITY_END());
}

void loop() {}
