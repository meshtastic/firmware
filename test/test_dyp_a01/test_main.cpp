// Pins DypA01Parser, dypA01MedianMm and dypA01BurstPublishable in
// src/modules/Telemetry/Sensor/DYPA01Protocol.h, plus EnvironmentMetrics.distance.
//
// The DYP-A01 driver publishes millimetres on the existing environment_metrics.distance
// field. There is no DYP sensor-type enum on that message, so a client already able to
// read RCWL9620 distance can read this sensor without a protobuf change.
//
// Regression guarded: a checksum failure or a missing frame must not become a distance;
// a short false echo must not be averaged into a distance the sensor never reported;
// fewer than DYP_A01_MIN_VALID_SAMPLES valid frames (the disconnected-sensor case)
// must not be treated as publishable.

#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include "modules/Telemetry/Sensor/DYPA01Protocol.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static bool feed(DypA01Parser &parser, const uint8_t *bytes, size_t count, float &distanceMm, bool &rejected)
{
    bool accepted = false;
    rejected = false;
    for (size_t i = 0; i < count; i++) {
        float sampleMm = 0;
        const DypA01Parse step = parser.push(bytes[i], sampleMm);
        if (step == DypA01Parse::Accepted) {
            distanceMm = sampleMm;
            accepted = true;
        } else if (step == DypA01Parse::Rejected) {
            rejected = true;
        }
    }
    return accepted;
}

// 1000 mm is 0x03E8. SUM = (0xFF + 0x03 + 0xE8) & 0xFF = 0xEA. Checked by hand so a
// broken encoder that matches a broken decoder cannot hide.
static const uint8_t kFrame1000mm[4] = {0xFF, 0x03, 0xE8, 0xEA};

void test_known_distances_decode_from_checksummed_frames()
{
    const uint16_t knownMm[] = {280, 1000, 6000, 7500};
    for (uint16_t mm : knownMm) {
        uint8_t frame[4];
        dypA01EncodeFrame(mm, frame);
        DypA01Parser parser;
        float decoded = -1;
        bool rejected = false;
        TEST_ASSERT_TRUE(feed(parser, frame, sizeof(frame), decoded, rejected));
        TEST_ASSERT_FALSE(rejected);
        TEST_ASSERT_EQUAL_FLOAT((float)mm, decoded);
    }

    uint8_t encoded[4];
    dypA01EncodeFrame(1000, encoded);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kFrame1000mm, encoded, 4);

    DypA01Parser parser;
    float decoded = -1;
    bool rejected = false;
    TEST_ASSERT_TRUE(feed(parser, kFrame1000mm, sizeof(kFrame1000mm), decoded, rejected));
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, decoded);
}

void test_invalid_frames_are_rejected()
{
    const uint8_t badSum[4] = {0xFF, 0x03, 0xE8, 0x00};
    DypA01Parser parser;
    float decoded = 1234;
    bool rejected = false;
    TEST_ASSERT_FALSE(feed(parser, badSum, sizeof(badSum), decoded, rejected));
    TEST_ASSERT_TRUE(rejected);
    TEST_ASSERT_EQUAL_FLOAT(1234.0f, decoded);

    const uint8_t noiseThenGood[] = {0x00, 0x11, 0x22, 0xFF, 0x03, 0xE8, 0xEA};
    decoded = -1;
    rejected = false;
    TEST_ASSERT_TRUE(feed(parser, noiseThenGood, sizeof(noiseThenGood), decoded, rejected));
    TEST_ASSERT_FALSE(rejected);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, decoded);

    // A truncated header swallows the next 0xFF; the rest of that frame must still resync.
    parser.reset();
    const uint8_t truncatedThenGood[] = {0xFF, 0x01, 0xFF, 0x03, 0xE8, 0xEA};
    decoded = -1;
    rejected = false;
    TEST_ASSERT_TRUE(feed(parser, truncatedThenGood, sizeof(truncatedThenGood), decoded, rejected));
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, decoded);
}

void test_disconnected_sensor_is_not_publishable()
{
    uint8_t silent[32];
    for (uint8_t &b : silent)
        b = 0x00;

    DypA01Parser parser;
    float decoded = -1;
    bool rejected = false;
    TEST_ASSERT_FALSE(feed(parser, silent, sizeof(silent), decoded, rejected));
    TEST_ASSERT_FALSE(rejected);
    TEST_ASSERT_FALSE(dypA01BurstPublishable(0));
    TEST_ASSERT_FALSE(dypA01BurstPublishable(DYP_A01_MIN_VALID_SAMPLES - 1));
    TEST_ASSERT_TRUE(dypA01BurstPublishable(DYP_A01_MIN_VALID_SAMPLES));
}

void test_median_keeps_a_real_sample_when_a_short_echo_is_present()
{
    // Bridge install: true water echo near 6 m, one multipath hit at 0.5 m.
    // The mean would land near 5.4 m, a distance the sensor did not report.
    float samples[] = {6000, 6000, 500, 6000, 6000, 6000, 6100, 5900, 6000};
    TEST_ASSERT_EQUAL_FLOAT(6000.0f, dypA01MedianMm(samples, 9));

    float even[] = {1000, 2000};
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, dypA01MedianMm(even, 2));
}

static bool roundTripDistance(float distanceMm, bool present, float &decodedMm, bool &decodedPresent)
{
    meshtastic_Telemetry in = meshtastic_Telemetry_init_zero;
    in.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    in.variant.environment_metrics.has_distance = present;
    in.variant.environment_metrics.distance = distanceMm;

    uint8_t buf[meshtastic_Telemetry_size];
    pb_ostream_t out = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&out, &meshtastic_Telemetry_msg, &in))
        return false;

    meshtastic_Telemetry got = meshtastic_Telemetry_init_zero;
    pb_istream_t inStream = pb_istream_from_buffer(buf, out.bytes_written);
    if (!pb_decode(&inStream, &meshtastic_Telemetry_msg, &got))
        return false;

    decodedPresent = got.variant.environment_metrics.has_distance;
    decodedMm = got.variant.environment_metrics.distance;
    return got.which_variant == meshtastic_Telemetry_environment_metrics_tag;
}

void test_environment_metrics_distance_round_trips_without_a_new_sensor_type()
{
    const float knownMm[] = {280.0f, 1000.0f, 6000.0f, 7500.0f};
    for (float mm : knownMm) {
        float decoded = -1;
        bool present = false;
        TEST_ASSERT_TRUE(roundTripDistance(mm, true, decoded, present));
        TEST_ASSERT_TRUE(present);
        TEST_ASSERT_EQUAL_FLOAT(mm, decoded);
    }

    float decoded = 1;
    bool present = true;
    TEST_ASSERT_TRUE(roundTripDistance(0, false, decoded, present));
    TEST_ASSERT_FALSE(present);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_known_distances_decode_from_checksummed_frames);
    RUN_TEST(test_invalid_frames_are_rejected);
    RUN_TEST(test_disconnected_sensor_is_not_publishable);
    RUN_TEST(test_median_keeps_a_real_sample_when_a_short_echo_is_present);
    RUN_TEST(test_environment_metrics_distance_round_trips_without_a_new_sensor_type);
    exit(UNITY_END());
}

void loop() {}
