#pragma once
#include "test_helpers.h"

// Helper function to create and encode position data
static size_t encode_position(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_Position position = meshtastic_Position_init_zero;
    position.latitude_i = 374428880;    // 37.4428880 * 1e7
    position.longitude_i = -1221913440; // -122.1913440 * 1e7
    position.altitude = 100;
    position.time = 1609459200;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_Position_msg, &position);
    return stream.bytes_written;
}

// Test POSITION_APP port serialization
void test_position_serialization()
{
    uint8_t buffer[128];
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

    delete root;
}
