#include "../test_helpers.h"

static size_t encode_position(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Position position = meshtastic_Position_init_zero;
    position.latitude_i = 374208000;
    position.longitude_i = -1221981000;
    position.altitude = 123;
    position.time = 1609459200;
    position.has_altitude = true;
    position.has_latitude_i = true;
    position.has_longitude_i = true;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Position_msg, &position);
    return stream.bytes_written;
}

void test_position_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_position(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_POSITION_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());

    TEST_ASSERT_TRUE(root.isMember("type"));
    TEST_ASSERT_EQUAL_STRING("position", root["type"].asString().c_str());

    TEST_ASSERT_TRUE(root.isMember("payload"));
    TEST_ASSERT_TRUE(root["payload"].isObject());

    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("latitude_i"));
    TEST_ASSERT_EQUAL(374208000, payload["latitude_i"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("longitude_i"));
    TEST_ASSERT_EQUAL(-1221981000, payload["longitude_i"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("altitude"));
    TEST_ASSERT_EQUAL(123, payload["altitude"].asInt());
}
