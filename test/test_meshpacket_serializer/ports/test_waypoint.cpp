#include "../test_helpers.h"

static size_t encode_waypoint(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
    waypoint.id = 12345;
    waypoint.latitude_i = 374208000;
    waypoint.longitude_i = -1221981000;
    waypoint.expire = 1609459200 + 3600;
    strcpy(waypoint.name, "Test Point");
    strcpy(waypoint.description, "Test waypoint description");

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Waypoint_msg, &waypoint);
    return stream.bytes_written;
}

void test_waypoint_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_waypoint(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_WAYPOINT_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());

    TEST_ASSERT_TRUE(root.isMember("type"));
    TEST_ASSERT_EQUAL_STRING("waypoint", root["type"].asString().c_str());

    TEST_ASSERT_TRUE(root.isMember("payload"));
    TEST_ASSERT_TRUE(root["payload"].isObject());

    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("id"));
    TEST_ASSERT_EQUAL(12345, payload["id"].asInt());

    TEST_ASSERT_TRUE(payload.isMember("name"));
    TEST_ASSERT_EQUAL_STRING("Test Point", payload["name"].asString().c_str());
}
