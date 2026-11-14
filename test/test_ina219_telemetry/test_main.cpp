// Pins the INA219 telemetry reader in src/modules/Telemetry/Sensor/INA219Telemetry.h,
// called by INA219Sensor::getMetrics(). PowerTelemetry passes a power_metrics variant,
// while EnvironmentTelemetry passes environment_metrics; each must receive voltage/current
// in its own protobuf fields. The former unconditional environment writes alias power
// channel 3 in the union, silently publishing the sensor on the wrong channel.
// A counting sensor also guards against reading I2C for unsupported telemetry variants.

#include "Arduino.h"
#include "TestUtil.h"
#include "modules/Telemetry/Sensor/INA219Telemetry.h"
#include <cstdlib>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <unity.h>

namespace
{
class CountingSensor
{
  public:
    float voltage = 5.12f;
    float current = 123.4f;
    unsigned voltageReads = 0;
    unsigned currentReads = 0;

    float getBusVoltage_V()
    {
        voltageReads++;
        return voltage;
    }

    float getCurrent_mA()
    {
        currentReads++;
        return current;
    }
};
} // namespace

void setUp() {}
void tearDown() {}

void test_power_metrics_encode_on_channel_one()
{
    CountingSensor sensor;
    meshtastic_Telemetry measurement = meshtastic_Telemetry_init_zero;
    measurement.which_variant = meshtastic_Telemetry_power_metrics_tag;

    TEST_ASSERT_TRUE(readIna219Metrics(sensor, measurement, 1.0f));
    TEST_ASSERT_EQUAL_UINT(1, sensor.voltageReads);
    TEST_ASSERT_EQUAL_UINT(1, sensor.currentReads);

    uint8_t bytes[meshtastic_Telemetry_size];
    pb_ostream_t output = pb_ostream_from_buffer(bytes, sizeof(bytes));
    TEST_ASSERT_TRUE(pb_encode(&output, &meshtastic_Telemetry_msg, &measurement));
    meshtastic_Telemetry decoded = meshtastic_Telemetry_init_zero;
    pb_istream_t input = pb_istream_from_buffer(bytes, output.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&input, &meshtastic_Telemetry_msg, &decoded));

    TEST_ASSERT_EQUAL_UINT(meshtastic_Telemetry_power_metrics_tag, decoded.which_variant);
    const auto &metrics = decoded.variant.power_metrics;
    TEST_ASSERT_TRUE(metrics.has_ch1_voltage);
    TEST_ASSERT_TRUE(metrics.has_ch1_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sensor.voltage, metrics.ch1_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sensor.current, metrics.ch1_current);
    TEST_ASSERT_FALSE(metrics.has_ch2_voltage);
    TEST_ASSERT_FALSE(metrics.has_ch2_current);
    TEST_ASSERT_FALSE(metrics.has_ch3_voltage);
    TEST_ASSERT_FALSE(metrics.has_ch3_current);
}

void test_environment_metrics_preserve_other_sensor_fields()
{
    CountingSensor sensor;
    meshtastic_Telemetry measurement = meshtastic_Telemetry_init_zero;
    measurement.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    measurement.variant.environment_metrics.has_temperature = true;
    measurement.variant.environment_metrics.temperature = 22.5f;

    TEST_ASSERT_TRUE(readIna219Metrics(sensor, measurement, 1.0f));
    TEST_ASSERT_EQUAL_UINT(1, sensor.voltageReads);
    TEST_ASSERT_EQUAL_UINT(1, sensor.currentReads);
    const auto &metrics = measurement.variant.environment_metrics;
    TEST_ASSERT_TRUE(metrics.has_voltage);
    TEST_ASSERT_TRUE(metrics.has_current);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sensor.voltage, metrics.voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sensor.current, metrics.current);
    TEST_ASSERT_TRUE(metrics.has_temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.5f, metrics.temperature);
}

void test_current_multiplier_applies_to_both_variants()
{
    CountingSensor sensor;
    sensor.current = -20.0f;
    const pb_size_t variants[] = {meshtastic_Telemetry_environment_metrics_tag, meshtastic_Telemetry_power_metrics_tag};
    for (const auto variant : variants) {
        meshtastic_Telemetry measurement = meshtastic_Telemetry_init_zero;
        measurement.which_variant = variant;
        TEST_ASSERT_TRUE(readIna219Metrics(sensor, measurement, 2.5f));
        const float current = variant == meshtastic_Telemetry_environment_metrics_tag
                                  ? measurement.variant.environment_metrics.current
                                  : measurement.variant.power_metrics.ch1_current;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, -50.0f, current);
    }
}

void test_power_metrics_preserve_other_sensor_channels()
{
    CountingSensor sensor;
    meshtastic_Telemetry measurement = meshtastic_Telemetry_init_zero;
    measurement.which_variant = meshtastic_Telemetry_power_metrics_tag;
    measurement.variant.power_metrics.has_ch2_voltage = true;
    measurement.variant.power_metrics.ch2_voltage = 12.0f;

    TEST_ASSERT_TRUE(readIna219Metrics(sensor, measurement, 1.0f));
    TEST_ASSERT_TRUE(measurement.variant.power_metrics.has_ch2_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, measurement.variant.power_metrics.ch2_voltage);
}

void test_unsupported_variants_do_not_read_or_modify_measurement()
{
    CountingSensor sensor;
    const pb_size_t variants[] = {0, meshtastic_Telemetry_device_metrics_tag, meshtastic_Telemetry_air_quality_metrics_tag};
    for (const auto variant : variants) {
        meshtastic_Telemetry measurement = meshtastic_Telemetry_init_zero;
        measurement.time = 0x12345678;
        measurement.which_variant = variant;
        uint8_t before[sizeof(measurement)];
        memcpy(before, &measurement, sizeof(measurement));

        TEST_ASSERT_FALSE(readIna219Metrics(sensor, measurement, 1.0f));
        TEST_ASSERT_EQUAL_MEMORY(before, &measurement, sizeof(measurement));
    }
    TEST_ASSERT_EQUAL_UINT(0, sensor.voltageReads);
    TEST_ASSERT_EQUAL_UINT(0, sensor.currentReads);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_power_metrics_encode_on_channel_one);
    RUN_TEST(test_environment_metrics_preserve_other_sensor_fields);
    RUN_TEST(test_current_multiplier_applies_to_both_variants);
    RUN_TEST(test_power_metrics_preserve_other_sensor_channels);
    RUN_TEST(test_unsupported_variants_do_not_read_or_modify_measurement);
    exit(UNITY_END());
}

void loop() {}
