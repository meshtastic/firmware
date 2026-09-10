// Pins the telemetry payload-fit ceiling asserted in src/mesh/mesh-pb-constants.h:
// meshtastic_Telemetry_size <= meshtastic_Constants_DATA_PAYLOAD_LEN.
//
// Why it is required: a meshtastic_Telemetry that encodes larger than DATA_PAYLOAD_LEN (233) does
// not fail loudly. pb_encode_to_bytes() returns 0, ProtobufModule::allocDataProtobuf() stores that
// 0 as decoded.payload.size, and a well-formed packet carrying nothing is transmitted or handed to
// the phone. The static_assert turns that into a build failure, but it compares two generated
// constants against each other - it never checks that nanopb's Telemetry_size is a size a real
// encode can actually reach. This test does, by filling the largest variant to its worst case and
// measuring what nanopb emits.
//
// Regression guarded: meshtastic/firmware#11797. Protobuf pull #6836 took Telemetry_size from 120
// to 272 by introducing HostMetrics with a 200 B operator-supplied user_string that portduino sends
// to the mesh, and #11741 took EnvironmentMetrics from 222 to 312 - which is what showed up in the
// field, as empty environment records replayed to a phone. Delete these assertions and the next
// size increase ships as silent empty packets again.

#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstring>
#include <pb_encode.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// host_metrics is the largest variant, so it is what sets Telemetry_size. A 0xFF fill is its worst
// case - every optional present, every varint at full width - and user_string is filled to its
// bound. If some other variant ever becomes the largest, the equality below is what says so.
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
