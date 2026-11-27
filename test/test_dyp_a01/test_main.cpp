// Pins DypA01Parser, dypA01MedianMm and dypA01BurstPublishable in
// src/modules/Telemetry/Sensor/DYPA01Protocol.h.
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
#include "modules/Telemetry/Sensor/DYPA01Protocol.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void encodeFrame(uint16_t distanceMm, uint8_t out[4])
{
    out[0] = 0xFF;
    out[1] = (uint8_t)(distanceMm >> 8);
    out[2] = (uint8_t)(distanceMm & 0xFF);
    out[3] = dypA01Checksum(out[1], out[2]);
}

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
        encodeFrame(mm, frame);
        DypA01Parser parser;
        float decoded = -1;
        bool rejected = false;
        TEST_ASSERT_TRUE(feed(parser, frame, sizeof(frame), decoded, rejected));
        TEST_ASSERT_FALSE(rejected);
        TEST_ASSERT_EQUAL_FLOAT((float)mm, decoded);
    }

    uint8_t encoded[4];
    encodeFrame(1000, encoded);
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

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_known_distances_decode_from_checksummed_frames);
    RUN_TEST(test_invalid_frames_are_rejected);
    RUN_TEST(test_disconnected_sensor_is_not_publishable);
    RUN_TEST(test_median_keeps_a_real_sample_when_a_short_echo_is_present);
    exit(UNITY_END());
}

void loop() {}
