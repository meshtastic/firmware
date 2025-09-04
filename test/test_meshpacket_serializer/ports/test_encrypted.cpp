#include "../test_helpers.h"

// Helper function for all encrypted packet assertions
void assert_encrypted_packet(const std::string &json, meshtastic_MeshPacket packet)
{
    // Parse and validate JSON
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Assert basic packet fields
    TEST_ASSERT_TRUE(jsonObj.find("from") != jsonObj.end());
    TEST_ASSERT_EQUAL(packet.from, (uint32_t)jsonObj.at("from")->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("to") != jsonObj.end());
    TEST_ASSERT_EQUAL(packet.to, (uint32_t)jsonObj.at("to")->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("id") != jsonObj.end());
    TEST_ASSERT_EQUAL(packet.id, (uint32_t)jsonObj.at("id")->AsNumber());

    // Assert encrypted data fields
    TEST_ASSERT_TRUE(jsonObj.find("bytes") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj.at("bytes")->IsString());

    TEST_ASSERT_TRUE(jsonObj.find("size") != jsonObj.end());
    TEST_ASSERT_EQUAL(packet.encrypted.size, (int)jsonObj.at("size")->AsNumber());

    // Assert hex encoding
    std::string encrypted_hex = jsonObj["bytes"]->AsString();
    TEST_ASSERT_EQUAL(packet.encrypted.size * 2, encrypted_hex.length());

    delete root;
}

// Test encrypted packet serialization
void test_encrypted_packet_serialization()
{
    const char *data = "encrypted_payload_data";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(data), strlen(data),
                           meshtastic_MeshPacket_encrypted_tag);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, packet);
}

// Test empty encrypted packet
void test_empty_encrypted_packet()
{
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, nullptr, 0, meshtastic_MeshPacket_encrypted_tag);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, packet);
}
