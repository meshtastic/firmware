#include "../test_helpers.h"

static void fill_all_env_metrics(meshtastic_Telemetry &telemetry)
{
    telemetry.variant.environment_metrics.temperature = 23.56f;
    telemetry.variant.environment_metrics.has_temperature = true;
    telemetry.variant.environment_metrics.relative_humidity = 65.43f;
    telemetry.variant.environment_metrics.has_relative_humidity = true;
    telemetry.variant.environment_metrics.barometric_pressure = 1013.27f;
    telemetry.variant.environment_metrics.has_barometric_pressure = true;

    telemetry.variant.environment_metrics.gas_resistance = 50.58f;
    telemetry.variant.environment_metrics.has_gas_resistance = true;
    telemetry.variant.environment_metrics.iaq = 120;
    telemetry.variant.environment_metrics.has_iaq = true;

    telemetry.variant.environment_metrics.voltage = 3.34f;
    telemetry.variant.environment_metrics.has_voltage = true;
    telemetry.variant.environment_metrics.current = 0.53f;
    telemetry.variant.environment_metrics.has_current = true;

    telemetry.variant.environment_metrics.lux = 450.12f;
    telemetry.variant.environment_metrics.has_lux = true;
    telemetry.variant.environment_metrics.white_lux = 380.95f;
    telemetry.variant.environment_metrics.has_white_lux = true;
    telemetry.variant.environment_metrics.ir_lux = 25.37f;
    telemetry.variant.environment_metrics.has_ir_lux = true;
    telemetry.variant.environment_metrics.uv_lux = 15.68f;
    telemetry.variant.environment_metrics.has_uv_lux = true;

    telemetry.variant.environment_metrics.distance = 150.29f;
    telemetry.variant.environment_metrics.has_distance = true;

    telemetry.variant.environment_metrics.wind_direction = 180;
    telemetry.variant.environment_metrics.has_wind_direction = true;
    telemetry.variant.environment_metrics.wind_speed = 5.52f;
    telemetry.variant.environment_metrics.has_wind_speed = true;
    telemetry.variant.environment_metrics.wind_gust = 8.24f;
    telemetry.variant.environment_metrics.has_wind_gust = true;
    telemetry.variant.environment_metrics.wind_lull = 2.13f;
    telemetry.variant.environment_metrics.has_wind_lull = true;

    telemetry.variant.environment_metrics.weight = 75.56f;
    telemetry.variant.environment_metrics.has_weight = true;

    telemetry.variant.environment_metrics.radiation = 0.13f;
    telemetry.variant.environment_metrics.has_radiation = true;

    telemetry.variant.environment_metrics.rainfall_1h = 2.57f;
    telemetry.variant.environment_metrics.has_rainfall_1h = true;
    telemetry.variant.environment_metrics.rainfall_24h = 15.89f;
    telemetry.variant.environment_metrics.has_rainfall_24h = true;

    telemetry.variant.environment_metrics.soil_moisture = 85;
    telemetry.variant.environment_metrics.has_soil_moisture = true;
    telemetry.variant.environment_metrics.soil_temperature = 18.54f;
    telemetry.variant.environment_metrics.has_soil_temperature = true;
}

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

static size_t encode_telemetry_environment_metrics_empty(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

static size_t encode_telemetry_environment_metrics(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = 1609459200;
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    fill_all_env_metrics(telemetry);

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Telemetry_msg, &telemetry);
    return stream.bytes_written;
}

static Json::Value serialize_and_get_payload(meshtastic_PortNum port, const uint8_t *buffer, size_t payload_size)
{
    meshtastic_MeshPacket packet = create_test_packet(port, buffer, payload_size);
    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());
    TEST_ASSERT_TRUE(root.isMember("payload"));
    TEST_ASSERT_TRUE(root["payload"].isObject());
    return root;
}

void test_telemetry_device_metrics_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_device_metrics(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);

    TEST_ASSERT_TRUE(root.isMember("type"));
    TEST_ASSERT_EQUAL_STRING("telemetry", root["type"].asString().c_str());

    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("battery_level"));
    TEST_ASSERT_EQUAL(85, payload["battery_level"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("voltage"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.72f, payload["voltage"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("channel_utilization"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.56f, payload["channel_utilization"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("uptime_seconds"));
    TEST_ASSERT_EQUAL(12345, payload["uptime_seconds"].asInt());
}

void test_telemetry_environment_metrics_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);
    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("temperature"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.56f, payload["temperature"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("relative_humidity"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.43f, payload["relative_humidity"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("distance"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.29f, payload["distance"].asFloat());
}

void test_telemetry_environment_metrics_comprehensive()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);
    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("temperature"));
    TEST_ASSERT_TRUE(payload.isMember("relative_humidity"));
    TEST_ASSERT_TRUE(payload.isMember("barometric_pressure"));
    TEST_ASSERT_TRUE(payload.isMember("gas_resistance"));
    TEST_ASSERT_TRUE(payload.isMember("voltage"));
    TEST_ASSERT_TRUE(payload.isMember("current"));
    TEST_ASSERT_TRUE(payload.isMember("iaq"));
    TEST_ASSERT_TRUE(payload.isMember("distance"));
    TEST_ASSERT_TRUE(payload.isMember("lux"));
    TEST_ASSERT_TRUE(payload.isMember("white_lux"));
    TEST_ASSERT_TRUE(payload.isMember("wind_direction"));
    TEST_ASSERT_TRUE(payload.isMember("wind_speed"));
    TEST_ASSERT_TRUE(payload.isMember("wind_gust"));
    TEST_ASSERT_TRUE(payload.isMember("wind_lull"));
    TEST_ASSERT_TRUE(payload.isMember("radiation"));
}

void test_telemetry_environment_metrics_missing_fields()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);
    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("ir_lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.37f, payload["ir_lux"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("uv_lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.68f, payload["uv_lux"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("weight"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.56f, payload["weight"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("rainfall_1h"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.57f, payload["rainfall_1h"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("rainfall_24h"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.89f, payload["rainfall_24h"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("soil_moisture"));
    TEST_ASSERT_EQUAL(85, payload["soil_moisture"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("soil_temperature"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.54f, payload["soil_temperature"].asFloat());
}

// Canary test: if a new env field is added to the protobuf but not to the serializer
// (or to fill_all_env_metrics), this test will fail.
void test_telemetry_environment_metrics_complete_coverage()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);
    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("temperature"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.56f, payload["temperature"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("relative_humidity"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.43f, payload["relative_humidity"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("barometric_pressure"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1013.27f, payload["barometric_pressure"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("gas_resistance"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.58f, payload["gas_resistance"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("iaq"));
    TEST_ASSERT_EQUAL(120, payload["iaq"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("voltage"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.34f, payload["voltage"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("current"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.53f, payload["current"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 450.12f, payload["lux"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("white_lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 380.95f, payload["white_lux"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("ir_lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.37f, payload["ir_lux"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("uv_lux"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.68f, payload["uv_lux"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("distance"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.29f, payload["distance"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("wind_direction"));
    TEST_ASSERT_EQUAL(180, payload["wind_direction"].asInt());
    TEST_ASSERT_TRUE(payload.isMember("wind_speed"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.52f, payload["wind_speed"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("wind_gust"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.24f, payload["wind_gust"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("wind_lull"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.13f, payload["wind_lull"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("weight"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.56f, payload["weight"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("radiation"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.13f, payload["radiation"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("rainfall_1h"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.57f, payload["rainfall_1h"].asFloat());
    TEST_ASSERT_TRUE(payload.isMember("rainfall_24h"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.89f, payload["rainfall_24h"].asFloat());

    TEST_ASSERT_TRUE(payload.isMember("soil_moisture"));
    TEST_ASSERT_EQUAL(85, payload["soil_moisture"].asInt());
    TEST_ASSERT_TRUE(payload.isMember("soil_temperature"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.54f, payload["soil_temperature"].asFloat());
}

void test_telemetry_environment_metrics_unset_fields()
{
    uint8_t buffer[256];
    size_t payload_size = encode_telemetry_environment_metrics_empty(buffer, sizeof(buffer));

    Json::Value root = serialize_and_get_payload(meshtastic_PortNum_TELEMETRY_APP, buffer, payload_size);
    const Json::Value &payload = root["payload"];

    TEST_ASSERT_FALSE(payload.isMember("temperature"));
    TEST_ASSERT_FALSE(payload.isMember("relative_humidity"));
    TEST_ASSERT_FALSE(payload.isMember("barometric_pressure"));
    TEST_ASSERT_FALSE(payload.isMember("gas_resistance"));
    TEST_ASSERT_FALSE(payload.isMember("iaq"));
    TEST_ASSERT_FALSE(payload.isMember("voltage"));
    TEST_ASSERT_FALSE(payload.isMember("current"));
    TEST_ASSERT_FALSE(payload.isMember("lux"));
    TEST_ASSERT_FALSE(payload.isMember("white_lux"));
    TEST_ASSERT_FALSE(payload.isMember("ir_lux"));
    TEST_ASSERT_FALSE(payload.isMember("uv_lux"));
    TEST_ASSERT_FALSE(payload.isMember("distance"));
    TEST_ASSERT_FALSE(payload.isMember("wind_direction"));
    TEST_ASSERT_FALSE(payload.isMember("wind_speed"));
    TEST_ASSERT_FALSE(payload.isMember("wind_gust"));
    TEST_ASSERT_FALSE(payload.isMember("wind_lull"));
    TEST_ASSERT_FALSE(payload.isMember("weight"));
    TEST_ASSERT_FALSE(payload.isMember("radiation"));
    TEST_ASSERT_FALSE(payload.isMember("rainfall_1h"));
    TEST_ASSERT_FALSE(payload.isMember("rainfall_24h"));
    TEST_ASSERT_FALSE(payload.isMember("soil_moisture"));
    TEST_ASSERT_FALSE(payload.isMember("soil_temperature"));
}
