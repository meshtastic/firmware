#include "../test_helpers.h"

static void verify_text_message_packet_structure(const std::string &json, const char *expected_text)
{
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());

    TEST_ASSERT_TRUE(root.isMember("from"));
    TEST_ASSERT_EQUAL(0x11223344u, root["from"].asUInt());
    TEST_ASSERT_TRUE(root.isMember("to"));
    TEST_ASSERT_EQUAL(0x55667788u, root["to"].asUInt());
    TEST_ASSERT_TRUE(root.isMember("id"));
    TEST_ASSERT_EQUAL(0x9999u, root["id"].asUInt());

    TEST_ASSERT_TRUE(root.isMember("type"));
    TEST_ASSERT_EQUAL_STRING("text", root["type"].asString().c_str());

    TEST_ASSERT_TRUE(root.isMember("payload"));
    TEST_ASSERT_TRUE(root["payload"].isObject());

    const Json::Value &payload = root["payload"];
    TEST_ASSERT_TRUE(payload.isMember("text"));
    TEST_ASSERT_EQUAL_STRING(expected_text, payload["text"].asString().c_str());
}

void test_text_message_serialization()
{
    const char *test_text = "Hello Meshtastic!";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(test_text), strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, test_text);
}

void test_text_message_serialization_null()
{
    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, nullptr, 0);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, "");
}

void test_text_message_serialization_long_text()
{
    constexpr size_t MAX_MESSAGE_SIZE = 200;
    std::string long_text(MAX_MESSAGE_SIZE, 'A');

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP,
                                                      reinterpret_cast<const uint8_t *>(long_text.c_str()), long_text.length());

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, long_text.c_str());
}

void test_text_message_serialization_oversized()
{
    constexpr size_t OVERSIZED_MESSAGE = 250;
    std::string oversized_text(OVERSIZED_MESSAGE, 'B');

    meshtastic_MeshPacket packet = create_test_packet(
        meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(oversized_text.c_str()), oversized_text.length());

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    std::string expected_text = oversized_text.substr(0, 234);
    verify_text_message_packet_structure(json, expected_text.c_str());
}

void test_text_message_serialization_invalid_utf8()
{
    const uint8_t invalid_utf8[] = {0xFF, 0xFE, 0xFD, 0x00};
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, invalid_utf8, sizeof(invalid_utf8) - 1);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);
}
