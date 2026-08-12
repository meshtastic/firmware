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

void test_nodeinfo_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_user_info(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_NODEINFO_APP, buffer, payload_size);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());

    TEST_ASSERT_TRUE(root.isMember("type"));
    TEST_ASSERT_EQUAL_STRING("nodeinfo", root["type"].asString().c_str());

    TEST_ASSERT_TRUE(root.isMember("payload"));
    TEST_ASSERT_TRUE(root["payload"].isObject());

    const Json::Value &payload = root["payload"];

    TEST_ASSERT_TRUE(payload.isMember("shortname"));
    TEST_ASSERT_EQUAL_STRING("TEST", payload["shortname"].asString().c_str());

    TEST_ASSERT_TRUE(payload.isMember("longname"));
    TEST_ASSERT_EQUAL_STRING("Test User", payload["longname"].asString().c_str());
}
