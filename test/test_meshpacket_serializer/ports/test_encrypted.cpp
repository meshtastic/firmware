#include "../test_helpers.h"

// test data initialization
const int from = 0x11223344;
const int to = 0x55667788;
const int id = 0x9999;

// Helper function to create a test encrypted packet
meshtastic_MeshPacket create_test_encrypted_packet(uint32_t from, uint32_t to, uint32_t id, const char *data)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = from;
    packet.to = to;
    packet.id = id;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    if (data) {
        packet.encrypted.size = strlen(data);
        memcpy(packet.encrypted.bytes, data, packet.encrypted.size);
    }

    return packet;
}

// Comprehensive helper function for all encrypted packet assertions
void assert_encrypted_packet(const std::string &json, uint32_t expected_from, uint32_t expected_to, uint32_t expected_id,
                             size_t expected_size)
{
    // Parse and validate JSON
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Assert basic packet fields
    TEST_ASSERT_TRUE(jsonObj.find("from") != jsonObj.end());
    TEST_ASSERT_EQUAL(expected_from, (uint32_t)jsonObj.at("from")->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("to") != jsonObj.end());
    TEST_ASSERT_EQUAL(expected_to, (uint32_t)jsonObj.at("to")->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("id") != jsonObj.end());
    TEST_ASSERT_EQUAL(expected_id, (uint32_t)jsonObj.at("id")->AsNumber());

    // Assert encrypted data fields
    TEST_ASSERT_TRUE(jsonObj.find("bytes") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj.at("bytes")->IsString());

    TEST_ASSERT_TRUE(jsonObj.find("size") != jsonObj.end());
    TEST_ASSERT_EQUAL(expected_size, (int)jsonObj.at("size")->AsNumber());

    // Assert hex encoding
    std::string encrypted_hex = jsonObj["bytes"]->AsString();
    TEST_ASSERT_EQUAL(expected_size * 2, encrypted_hex.length());

    delete root;
}

// Test encrypted packet serialization
void test_encrypted_packet_serialization()
{
    const char *data = "encrypted_payload_data";

    meshtastic_MeshPacket packet = create_test_encrypted_packet(from, to, id, data);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, from, to, id, strlen(data));
}

// Test empty encrypted packet
void test_empty_encrypted_packet()
{
    const char *data = "";

    meshtastic_MeshPacket packet = create_test_encrypted_packet(from, to, id, data);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, from, to, id, strlen(data));
}
