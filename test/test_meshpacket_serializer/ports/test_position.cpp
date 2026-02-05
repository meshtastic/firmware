#include "../test_helpers.h"

static size_t encode_position(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Position position = meshtastic_Position_init_zero;
    position.latitude_i = 374208000;    // 37.4208 degrees * 1e7
    position.longitude_i = -1221981000; // -122.1981 degrees * 1e7
    position.altitude = 123;
    position.time = 1609459200;
    position.has_altitude = true;
    position.has_latitude_i = true;
    position.has_longitude_i = true;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Position_msg, &position);
    return stream.bytes_written;
}

// Test POSITION_APP port
void test_position_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_position(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_POSITION_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("position", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Verify position data
    TEST_ASSERT_TRUE(payload.find("latitude_i") != payload.end());
    TEST_ASSERT_EQUAL(374208000, (int)payload["latitude_i"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("longitude_i") != payload.end());
    TEST_ASSERT_EQUAL(-1221981000, (int)payload["longitude_i"]->AsNumber());

    TEST_ASSERT_TRUE(payload.find("altitude") != payload.end());
    TEST_ASSERT_EQUAL(123, (int)payload["altitude"]->AsNumber());

    delete root;
}
