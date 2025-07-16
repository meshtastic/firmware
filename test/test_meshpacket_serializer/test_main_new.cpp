#include <Arduino.h>
#include <unity.h>

// Include all modular test files
#include "ports/test_encrypted.cpp"
#include "ports/test_nodeinfo.cpp"
#include "ports/test_position.cpp"
#include "ports/test_telemetry.cpp"
#include "ports/test_text_message.cpp"
#include "ports/test_waypoint.cpp"

void setup()
{
    UNITY_BEGIN();

    // Text message tests
    RUN_TEST(test_text_message_serialization);

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

    UNITY_END();
}

void loop()
{
    delay(1000);
}
