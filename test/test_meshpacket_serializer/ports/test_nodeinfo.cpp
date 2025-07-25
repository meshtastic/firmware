#include "../test_helpers.h"

static size_t encode_user_info(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_User user = meshtastic_User_init_zero;
    strcpy(user.short_name, "TEST");
    strcpy(user.long_name, "Test User");
    strcpy(user.id, "!12345678");
    user.hw_model = meshtastic_HardwareModel_HELTEC_V3;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_User_msg, &user);
    return stream.bytes_written;
}

// Test NODEINFO_APP port
void test_nodeinfo_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_user_info(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_NODEINFO_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("nodeinfo", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();

    // Verify user data
    TEST_ASSERT_TRUE(payload.find("shortname") != payload.end());
    TEST_ASSERT_EQUAL_STRING("TEST", payload["shortname"]->AsString().c_str());

    TEST_ASSERT_TRUE(payload.find("longname") != payload.end());
    TEST_ASSERT_EQUAL_STRING("Test User", payload["longname"]->AsString().c_str());

    delete root;
}
