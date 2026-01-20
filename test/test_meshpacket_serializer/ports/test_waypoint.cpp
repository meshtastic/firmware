#include "../test_helpers.h"

static size_t encode_waypoint(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
    waypoint.id = 12345;
    waypoint.latitude_i = 374208000;
    waypoint.longitude_i = -1221981000;
    waypoint.expire = 1609459200 + 3600; // 1 hour from now
    strcpy(waypoint.name, "Test Point");
    strcpy(waypoint.description, "Test waypoint description");

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Waypoint_msg, &waypoint);
    return stream.bytes_written;
}

// Test WAYPOINT_APP port
void test_waypoint_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_waypoint(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_WAYPOINT_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("waypoint", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Verify waypoint data
    TEST_ASSERT_TRUE(payload.find("id") != payload.end());
    TEST_ASSERT_EQUAL(12345, (int)payload["id"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("name") != payload.end());
    TEST_ASSERT_EQUAL_STRING("Test Point", payload["name"]->AsString().c_str());

    delete root;
}
