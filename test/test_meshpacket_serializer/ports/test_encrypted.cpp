#include "../test_helpers.h"

static void assert_encrypted_packet(const std::string &json, const meshtastic_MeshPacket &packet)
{
    TEST_ASSERT_TRUE(json.length() > 0);

    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isObject());

    TEST_ASSERT_TRUE(root.isMember("from"));
    TEST_ASSERT_EQUAL(packet.from, root["from"].asUInt());

    TEST_ASSERT_TRUE(root.isMember("to"));
    TEST_ASSERT_EQUAL(packet.to, root["to"].asUInt());

    TEST_ASSERT_TRUE(root.isMember("id"));
    TEST_ASSERT_EQUAL(packet.id, root["id"].asUInt());

    TEST_ASSERT_TRUE(root.isMember("bytes"));
    TEST_ASSERT_TRUE(root["bytes"].isString());

    TEST_ASSERT_TRUE(root.isMember("size"));
    TEST_ASSERT_EQUAL(packet.encrypted.size, (int)root["size"].asInt());

    std::string encrypted_hex = root["bytes"].asString();
    TEST_ASSERT_EQUAL(packet.encrypted.size * 2, encrypted_hex.length());
}

void test_encrypted_packet_serialization()
{
    const char *data = "encrypted_payload_data";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(data), strlen(data),
                           meshtastic_MeshPacket_encrypted_tag);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, packet);
}

void test_empty_encrypted_packet()
{
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, nullptr, 0, meshtastic_MeshPacket_encrypted_tag);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, packet);
}
