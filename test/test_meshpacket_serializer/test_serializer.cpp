#include "test_helpers.h"
#include <Arduino.h>
#include <unity.h>

// Forward declarations for test functions
void test_text_message_serialization();
void test_text_message_serialization_null();
void test_text_message_serialization_long_text();
void test_text_message_serialization_oversized();
void test_text_message_serialization_invalid_utf8();
void test_position_serialization();
void test_nodeinfo_serialization();
void test_waypoint_serialization();
void test_telemetry_device_metrics_serialization();
void test_telemetry_environment_metrics_serialization();
void test_telemetry_environment_metrics_comprehensive();
void test_telemetry_environment_metrics_missing_fields();
void test_telemetry_environment_metrics_complete_coverage();
void test_telemetry_environment_metrics_unset_fields();
void test_encrypted_packet_serialization();
void test_empty_encrypted_packet();
void test_dmshell_open_roundtrip();
void test_dmshell_input_roundtrip();
void test_dmshell_resize_roundtrip();
void test_dmshell_close_roundtrip();

void setup()
{
    UNITY_BEGIN();

    // Text message tests
    RUN_TEST(test_text_message_serialization);
    RUN_TEST(test_text_message_serialization_null);
    RUN_TEST(test_text_message_serialization_long_text);
    RUN_TEST(test_text_message_serialization_oversized);
    RUN_TEST(test_text_message_serialization_invalid_utf8);

    // Position tests
    RUN_TEST(test_position_serialization);

    // Nodeinfo tests
    RUN_TEST(test_nodeinfo_serialization);

    // Waypoint tests
    RUN_TEST(test_waypoint_serialization);

    // Telemetry tests
    RUN_TEST(test_telemetry_device_metrics_serialization);
    RUN_TEST(test_telemetry_environment_metrics_serialization);
    RUN_TEST(test_telemetry_environment_metrics_comprehensive);
    RUN_TEST(test_telemetry_environment_metrics_missing_fields);
    RUN_TEST(test_telemetry_environment_metrics_complete_coverage);
    RUN_TEST(test_telemetry_environment_metrics_unset_fields);

    // Encrypted packet test
    RUN_TEST(test_encrypted_packet_serialization);
    RUN_TEST(test_empty_encrypted_packet);

    // DMShell protobuf transport tests
    RUN_TEST(test_dmshell_open_roundtrip);
    RUN_TEST(test_dmshell_input_roundtrip);
    RUN_TEST(test_dmshell_resize_roundtrip);
    RUN_TEST(test_dmshell_close_roundtrip);

    UNITY_END();
}

void loop()
{
    delay(1000);
}
