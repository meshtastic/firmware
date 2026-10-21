// Pins the static_assert in src/mesh/mesh-pb-constants.h that meshtastic_Telemetry_size fits
// meshtastic_Constants_DATA_PAYLOAD_LEN. That assert compares two generated constants and cannot
// tell whether Telemetry_size is a size a real encode reaches, so this fills the largest variant
// to its worst case and measures what nanopb emits.
//
// Regression guarded (#11797): an oversized Telemetry fails silently - pb_encode_to_bytes()
// returns 0 and a well-formed packet carrying nothing goes out. Relax this and the next protobuf
// size increase ships as empty packets again.

#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstring>
#include <pb_encode.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// host_metrics is the largest variant, so it sets Telemetry_size; 0xFF is its worst case, every
// optional present and every varint at full width.
void test_largest_telemetry_matches_generated_size_and_fits_payload()
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
    t.time = 0xFFFFFFFF; // proto3 omits a zero scalar; force the 5 B time field to be emitted
    t.which_variant = meshtastic_Telemetry_host_metrics_tag;
    memset(&t.variant.host_metrics, 0xFF, sizeof(t.variant.host_metrics));
    memset(t.variant.host_metrics.user_string, 'x', sizeof(t.variant.host_metrics.user_string) - 1);
    t.variant.host_metrics.user_string[sizeof(t.variant.host_metrics.user_string) - 1] = '\0';

    size_t size = 0;
    TEST_ASSERT_TRUE(pb_get_encoded_size(&size, &meshtastic_Telemetry_msg, &t));
    TEST_ASSERT_EQUAL_size_t(meshtastic_Telemetry_size, size);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(meshtastic_Constants_DATA_PAYLOAD_LEN, size);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_largest_telemetry_matches_generated_size_and_fits_payload);
    exit(UNITY_END());
}

void loop() {}
