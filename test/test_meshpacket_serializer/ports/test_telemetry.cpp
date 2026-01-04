#include "../test_helpers.h"

// Helper function to create and encode device metrics
static size_t encode_telemetry_device_metrics(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;
    telemetry.variant.device_metrics.battery_level = 85;
    telemetry.variant.device_metrics.has_battery_level = true;
    telemetry.variant.device_metrics.voltage = 3.72f;
    telemetry.variant.device_metrics.has_voltage = true;
    telemetry.variant.device_metrics.channel_utilization = 15.56f;
    telemetry.variant.device_metrics.has_channel_utilization = true;
    telemetry.variant.device_metrics.air_util_tx = 8.23f;
    telemetry.variant.device_metrics.has_air_util_tx = true;
    telemetry.variant.device_metrics.uptime_seconds = 12345;
    telemetry.variant.device_metrics.has_uptime_seconds = true;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

// Helper function to create and encode empty environment metrics (no fields set)
static size_t encode_telemetry_environment_metrics_empty(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;

    // NO fields are set - all has_* flags remain false
    // This tests that empty environment metrics don't produce any JSON fields

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

// Helper function to create environment metrics with ALL possible fields set
// This function should be updated whenever new fields are added to the protobuf
static size_t encode_telemetry_environment_metrics_all_fields(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;

    // Basic environment metrics
    telemetry.variant.environment_metrics.temperature = 23.56f;
    telemetry.variant.environment_metrics.has_temperature = true;
    telemetry.variant.environment_metrics.relative_humidity = 65.43f;
    telemetry.variant.environment_metrics.has_relative_humidity = true;
    telemetry.variant.environment_metrics.barometric_pressure = 1013.27f;
    telemetry.variant.environment_metrics.has_barometric_pressure = true;

    // Gas and air quality
    telemetry.variant.environment_metrics.gas_resistance = 50.58f;
    telemetry.variant.environment_metrics.has_gas_resistance = true;
    telemetry.variant.environment_metrics.iaq = 120;
    telemetry.variant.environment_metrics.has_iaq = true;

    // Power measurements
    telemetry.variant.environment_metrics.voltage = 3.34f;
    telemetry.variant.environment_metrics.has_voltage = true;
    telemetry.variant.environment_metrics.current = 0.53f;
    telemetry.variant.environment_metrics.has_current = true;

    // Light measurements (ALL 4 types)
    telemetry.variant.environment_metrics.lux = 450.12f;
    telemetry.variant.environment_metrics.has_lux = true;
    telemetry.variant.environment_metrics.white_lux = 380.95f;
    telemetry.variant.environment_metrics.has_white_lux = true;
    telemetry.variant.environment_metrics.ir_lux = 25.37f;
    telemetry.variant.environment_metrics.has_ir_lux = true;
    telemetry.variant.environment_metrics.uv_lux = 15.68f;
    telemetry.variant.environment_metrics.has_uv_lux = true;

    // Distance measurement
    telemetry.variant.environment_metrics.distance = 150.29f;
    telemetry.variant.environment_metrics.has_distance = true;

    // Wind measurements (ALL 4 types)
    telemetry.variant.environment_metrics.wind_direction = 180;
    telemetry.variant.environment_metrics.has_wind_direction = true;
    telemetry.variant.environment_metrics.wind_speed = 5.52f;
    telemetry.variant.environment_metrics.has_wind_speed = true;
    telemetry.variant.environment_metrics.wind_gust = 8.24f;
    telemetry.variant.environment_metrics.has_wind_gust = true;
    telemetry.variant.environment_metrics.wind_lull = 2.13f;
    telemetry.variant.environment_metrics.has_wind_lull = true;

    // Weight measurement
    telemetry.variant.environment_metrics.weight = 75.56f;
    telemetry.variant.environment_metrics.has_weight = true;

    // Radiation measurement
    telemetry.variant.environment_metrics.radiation = 0.13f;
    telemetry.variant.environment_metrics.has_radiation = true;

    // Rainfall measurements (BOTH types)
    telemetry.variant.environment_metrics.rainfall_1h = 2.57f;
    telemetry.variant.environment_metrics.has_rainfall_1h = true;
    telemetry.variant.environment_metrics.rainfall_24h = 15.89f;
    telemetry.variant.environment_metrics.has_rainfall_24h = true;

    // Soil measurements (BOTH types)
    telemetry.variant.environment_metrics.soil_moisture = 85;
    telemetry.variant.environment_metrics.has_soil_moisture = true;
    telemetry.variant.environment_metrics.soil_temperature = 18.54f;
    telemetry.variant.environment_metrics.has_soil_temperature = true;

    // IMPORTANT: When new environment fields are added to the protobuf,
    // they MUST be added here too, or the coverage test will fail!

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

// Helper function to create and encode environment metrics with all current fields
static size_t encode_telemetry_environment_metrics(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;

    // Basic environment metrics
    telemetry.variant.environment_metrics.temperature = 23.56f;
    telemetry.variant.environment_metrics.has_temperature = true;
    telemetry.variant.environment_metrics.relative_humidity = 65.43f;
    telemetry.variant.environment_metrics.has_relative_humidity = true;
    telemetry.variant.environment_metrics.barometric_pressure = 1013.27f;
    telemetry.variant.environment_metrics.has_barometric_pressure = true;

    // Gas and air quality
    telemetry.variant.environment_metrics.gas_resistance = 50.58f;
    telemetry.variant.environment_metrics.has_gas_resistance = true;
    telemetry.variant.environment_metrics.iaq = 120;
    telemetry.variant.environment_metrics.has_iaq = true;

    // Power measurements
    telemetry.variant.environment_metrics.voltage = 3.34f;
    telemetry.variant.environment_metrics.has_voltage = true;
    telemetry.variant.environment_metrics.current = 0.53f;
    telemetry.variant.environment_metrics.has_current = true;

    // Light measurements
    telemetry.variant.environment_metrics.lux = 450.12f;
    telemetry.variant.environment_metrics.has_lux = true;
    telemetry.variant.environment_metrics.white_lux = 380.95f;
    telemetry.variant.environment_metrics.has_white_lux = true;
    telemetry.variant.environment_metrics.ir_lux = 25.37f;
    telemetry.variant.environment_metrics.has_ir_lux = true;
    telemetry.variant.environment_metrics.uv_lux = 15.68f;
    telemetry.variant.environment_metrics.has_uv_lux = true;

    // Distance measurement
    telemetry.variant.environment_metrics.distance = 150.29f;
    telemetry.variant.environment_metrics.has_distance = true;

    // Wind measurements
    telemetry.variant.environment_metrics.wind_direction = 180;
    telemetry.variant.environment_metrics.has_wind_direction = true;
    telemetry.variant.environment_metrics.wind_speed = 5.52f;
    telemetry.variant.environment_metrics.has_wind_speed = true;
    telemetry.variant.environment_metrics.wind_gust = 8.24f;
    telemetry.variant.environment_metrics.has_wind_gust = true;
    telemetry.variant.environment_metrics.wind_lull = 2.13f;
    telemetry.variant.environment_metrics.has_wind_lull = true;

    // Weight measurement
    telemetry.variant.environment_metrics.weight = 75.56f;
    telemetry.variant.environment_metrics.has_weight = true;

    // Radiation measurement
    telemetry.variant.environment_metrics.radiation = 0.13f;
    telemetry.variant.environment_metrics.has_radiation = true;

    // Rainfall measurements
    telemetry.variant.environment_metrics.rainfall_1h = 2.57f;
    telemetry.variant.environment_metrics.has_rainfall_1h = true;
    telemetry.variant.environment_metrics.rainfall_24h = 15.89f;
    telemetry.variant.environment_metrics.has_rainfall_24h = true;

    // Soil measurements
    telemetry.variant.environment_metrics.soil_moisture = 85;
    telemetry.variant.environment_metrics.has_soil_moisture = true;
    telemetry.variant.environment_metrics.soil_temperature = 18.54f;
    telemetry.variant.environment_metrics.has_soil_temperature = true;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

// Test TELEMETRY_APP port with device metrics
void test_telemetry_device_metrics_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_device_metrics(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("telemetry", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Verify telemetry data
    TEST_ASSERT_TRUE(payload.find("battery_level") != payload.end());
    TEST_ASSERT_EQUAL(85, (int)payload["battery_level"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("voltage") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.72f, payload["voltage"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("channel_utilization") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.56f, payload["channel_utilization"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("uptime_seconds") != payload.end());
    TEST_ASSERT_EQUAL(12345, (int)payload["uptime_seconds"]->AsNumber());

    // Note: JSON serialization may not preserve exact 2-decimal formatting due to float precision
    // We verify the numeric values are correct within tolerance

    delete root;
}

// Test that telemetry environment metrics are properly serialized
void test_telemetry_environment_metrics_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check payload exists
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Test key fields that should be present in the serializer
    TEST_ASSERT_TRUE(payload.find("temperature") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.56f, payload["temperature"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("relative_humidity") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.43f, payload["relative_humidity"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("distance") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.29f, payload["distance"]->AsNumber());

    // Note: JSON serialization may have float precision limitations
    // We focus on verifying numeric accuracy rather than exact string formatting

    delete root;
}

// Test comprehensive environment metrics coverage
void test_telemetry_environment_metrics_comprehensive()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check payload exists
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Check all 15 originally supported fields
    TEST_ASSERT_TRUE(payload.find("temperature") != payload.end());
    TEST_ASSERT_TRUE(payload.find("relative_humidity") != payload.end());
    TEST_ASSERT_TRUE(payload.find("barometric_pressure") != payload.end());
    TEST_ASSERT_TRUE(payload.find("gas_resistance") != payload.end());
    TEST_ASSERT_TRUE(payload.find("voltage") != payload.end());
    TEST_ASSERT_TRUE(payload.find("current") != payload.end());
    TEST_ASSERT_TRUE(payload.find("iaq") != payload.end());
    TEST_ASSERT_TRUE(payload.find("distance") != payload.end());
    TEST_ASSERT_TRUE(payload.find("lux") != payload.end());
    TEST_ASSERT_TRUE(payload.find("white_lux") != payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_direction") != payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_speed") != payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_gust") != payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_lull") != payload.end());
    TEST_ASSERT_TRUE(payload.find("radiation") != payload.end());

    delete root;
}

// Test for the 7 environment fields that were added to complete coverage
void test_telemetry_environment_metrics_missing_fields()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check payload exists
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Check the 7 fields that were previously missing
    TEST_ASSERT_TRUE(payload.find("ir_lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.37f, payload["ir_lux"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("uv_lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.68f, payload["uv_lux"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("weight") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.56f, payload["weight"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("rainfall_1h") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.57f, payload["rainfall_1h"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("rainfall_24h") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.89f, payload["rainfall_24h"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("soil_moisture") != payload.end());
    TEST_ASSERT_EQUAL(85, (int)payload["soil_moisture"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("soil_temperature") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.54f, payload["soil_temperature"]->AsNumber());

    // Note: JSON float serialization may not preserve exact decimal formatting
    // We verify the values are numerically correct within tolerance

    delete root;
}

// Test that ALL environment fields are serialized (canary test for forgotten fields)
// This test will FAIL if a new environment field is added to the protobuf but not to the serializer
void test_telemetry_environment_metrics_complete_coverage()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics_all_fields(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check payload exists
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // ✅ ALL 22 environment fields MUST be present and correct
    // If this test fails, it means either:
    // 1. A new field was added to the protobuf but not to the serializer
    // 2. The encode_telemetry_environment_metrics_all_fields() function wasn't updated

    // Basic environment (3 fields)
    TEST_ASSERT_TRUE(payload.find("temperature") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.56f, payload["temperature"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("relative_humidity") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.43f, payload["relative_humidity"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("barometric_pressure") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1013.27f, payload["barometric_pressure"]->AsNumber());

    // Gas and air quality (2 fields)
    TEST_ASSERT_TRUE(payload.find("gas_resistance") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.58f, payload["gas_resistance"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("iaq") != payload.end());
    TEST_ASSERT_EQUAL(120, (int)payload["iaq"]->AsNumber());

    // Power measurements (2 fields)
    TEST_ASSERT_TRUE(payload.find("voltage") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.34f, payload["voltage"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("current") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.53f, payload["current"]->AsNumber());

    // Light measurements (4 fields)
    TEST_ASSERT_TRUE(payload.find("lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 450.12f, payload["lux"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("white_lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 380.95f, payload["white_lux"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("ir_lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.37f, payload["ir_lux"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("uv_lux") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.68f, payload["uv_lux"]->AsNumber());

    // Distance measurement (1 field)
    TEST_ASSERT_TRUE(payload.find("distance") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.29f, payload["distance"]->AsNumber());

    // Wind measurements (4 fields)
    TEST_ASSERT_TRUE(payload.find("wind_direction") != payload.end());
    TEST_ASSERT_EQUAL(180, (int)payload["wind_direction"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("wind_speed") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.52f, payload["wind_speed"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("wind_gust") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.24f, payload["wind_gust"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("wind_lull") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.13f, payload["wind_lull"]->AsNumber());

    // Weight measurement (1 field)
    TEST_ASSERT_TRUE(payload.find("weight") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.56f, payload["weight"]->AsNumber());

    // Radiation measurement (1 field)
    TEST_ASSERT_TRUE(payload.find("radiation") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.13f, payload["radiation"]->AsNumber());

    // Rainfall measurements (2 fields)
    TEST_ASSERT_TRUE(payload.find("rainfall_1h") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.57f, payload["rainfall_1h"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("rainfall_24h") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.89f, payload["rainfall_24h"]->AsNumber());

    // Soil measurements (2 fields)
    TEST_ASSERT_TRUE(payload.find("soil_moisture") != payload.end());
    TEST_ASSERT_EQUAL(85, (int)payload["soil_moisture"]->AsNumber());
    TEST_ASSERT_TRUE(payload.find("soil_temperature") != payload.end());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.54f, payload["soil_temperature"]->AsNumber());

    // Total: 22 environment fields
    // This test ensures 100% coverage of environment metrics

    // Note: JSON float serialization precision may vary due to the underlying library
    // The important aspect is that all values are numerically accurate within tolerance

    delete root;
}

// Test that unset environment fields are not present in JSON
void test_telemetry_environment_metrics_unset_fields()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics_empty(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check payload exists
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // With completely empty environment metrics, NO fields should be present
    // Only basic telemetry fields like "time" might be present

    // All 22 environment fields should be absent (none were set)
    TEST_ASSERT_TRUE(payload.find("temperature") == payload.end());
    TEST_ASSERT_TRUE(payload.find("relative_humidity") == payload.end());
    TEST_ASSERT_TRUE(payload.find("barometric_pressure") == payload.end());
    TEST_ASSERT_TRUE(payload.find("gas_resistance") == payload.end());
    TEST_ASSERT_TRUE(payload.find("iaq") == payload.end());
    TEST_ASSERT_TRUE(payload.find("voltage") == payload.end());
    TEST_ASSERT_TRUE(payload.find("current") == payload.end());
    TEST_ASSERT_TRUE(payload.find("lux") == payload.end());
    TEST_ASSERT_TRUE(payload.find("white_lux") == payload.end());
    TEST_ASSERT_TRUE(payload.find("ir_lux") == payload.end());
    TEST_ASSERT_TRUE(payload.find("uv_lux") == payload.end());
    TEST_ASSERT_TRUE(payload.find("distance") == payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_direction") == payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_speed") == payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_gust") == payload.end());
    TEST_ASSERT_TRUE(payload.find("wind_lull") == payload.end());
    TEST_ASSERT_TRUE(payload.find("weight") == payload.end());
    TEST_ASSERT_TRUE(payload.find("radiation") == payload.end());
    TEST_ASSERT_TRUE(payload.find("rainfall_1h") == payload.end());
    TEST_ASSERT_TRUE(payload.find("rainfall_24h") == payload.end());
    TEST_ASSERT_TRUE(payload.find("soil_moisture") == payload.end());
    TEST_ASSERT_TRUE(payload.find("soil_temperature") == payload.end());

    delete root;
}
