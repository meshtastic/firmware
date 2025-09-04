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
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = 0x11223344;
    packet.to = 0x55667788;
    packet.id = 0x9999;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    // Add some dummy encrypted data
    const char *encrypted_data = "encrypted_payload_data";
    packet.encrypted.size = strlen(encrypted_data);
    memcpy(packet.encrypted.bytes, encrypted_data, packet.encrypted.size);

    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check basic packet fields
    TEST_ASSERT_TRUE(jsonObj.find("from") != jsonObj.end());
    TEST_ASSERT_EQUAL(0x11223344, (uint32_t)jsonObj["from"]->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("to") != jsonObj.end());
    TEST_ASSERT_EQUAL(0x55667788, (uint32_t)jsonObj["to"]->AsNumber());

    TEST_ASSERT_TRUE(jsonObj.find("id") != jsonObj.end());
    TEST_ASSERT_EQUAL(0x9999, (uint32_t)jsonObj["id"]->AsNumber());

    // Check that it has encrypted data fields (not "payload" but "bytes" and "size")
    TEST_ASSERT_TRUE(jsonObj.find("bytes") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["bytes"]->IsString());

    TEST_ASSERT_TRUE(jsonObj.find("size") != jsonObj.end());
    TEST_ASSERT_EQUAL(22, (int)jsonObj["size"]->AsNumber()); // strlen("encrypted_payload_data") = 22

    // The encrypted data should be hex-encoded
    std::string encrypted_hex = jsonObj["bytes"]->AsString();
    TEST_ASSERT_TRUE(encrypted_hex.length() > 0);
    // Should be twice the size of the original data (hex encoding)
    TEST_ASSERT_EQUAL(44, encrypted_hex.length()); // 22 * 2 = 44

    delete root;
}

// Test empty encrypted packet
void test_empty_encrypted_packet()
{
    const char *data = "";

    meshtastic_MeshPacket packet = create_test_encrypted_packet(from, to, id, data);
    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);

    assert_encrypted_packet(json, packet);
}
