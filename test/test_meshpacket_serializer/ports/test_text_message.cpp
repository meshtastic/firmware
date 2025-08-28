#include "../test_helpers.h"
#include <memory>

// Helper function to test common packet fields and structure
void verify_text_message_packet_structure(const std::string &json, const char *expected_text)
{
    TEST_ASSERT_TRUE(json.length() > 0);

    // Use smart pointer for automatic memory management
    std::unique_ptr<JSONValue> root(JSON::Parse(json.c_str()));
    TEST_ASSERT_NOT_NULL(root.get());
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check basic packet fields - use helper function to reduce duplication
    auto check_field = [&](const char *field, uint32_t expected_value) {
        auto it = jsonObj.find(field);
        TEST_ASSERT_TRUE(it != jsonObj.end());
        TEST_ASSERT_EQUAL(expected_value, (uint32_t)it->second->AsNumber());
    };

    check_field("from", 0x11223344);
    check_field("to", 0x55667788);
    check_field("id", 0x9999);

    // Check message type
    auto type_it = jsonObj.find("type");
    TEST_ASSERT_TRUE(type_it != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("text", type_it->second->AsString().c_str());

    // Check payload
    auto payload_it = jsonObj.find("payload");
    TEST_ASSERT_TRUE(payload_it != jsonObj.end());
    TEST_ASSERT_TRUE(payload_it->second->IsObject());

    JSONObject payload = payload_it->second->AsObject();
    auto text_it = payload.find("text");
    TEST_ASSERT_TRUE(text_it != payload.end());
    TEST_ASSERT_EQUAL_STRING(expected_text, text_it->second->AsString().c_str());

    // No need for manual delete with smart pointer
}

// Test TEXT_MESSAGE_APP port
void test_text_message_serialization()
{
    const char *test_text = "Hello Meshtastic!";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(test_text), strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, test_text);
}

// Test with nullptr to check robustness
void test_text_message_serialization_null()
{
    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, nullptr, 0);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, "");
}

// Test TEXT_MESSAGE_APP port with very long message (boundary testing)
void test_text_message_serialization_long_text()
{
    // Test with actual message size limits
    constexpr size_t MAX_MESSAGE_SIZE = 200; // Typical LoRa payload limit
    std::string long_text(MAX_MESSAGE_SIZE, 'A');

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP,
                                                      reinterpret_cast<const uint8_t *>(long_text.c_str()), long_text.length());

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    verify_text_message_packet_structure(json, long_text.c_str());
}

// Test with message over size limit (should fail)
void test_text_message_serialization_oversized()
{
    constexpr size_t OVERSIZED_MESSAGE = 250; // Over the limit
    std::string oversized_text(OVERSIZED_MESSAGE, 'B');

    meshtastic_MeshPacket packet = create_test_packet(
        meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(oversized_text.c_str()), oversized_text.length());

    // Should fail or return empty/error
    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    // Should only verify first 234 characters for oversized messages
    std::string expected_text = oversized_text.substr(0, 234);
    verify_text_message_packet_structure(json, expected_text.c_str());
}

// Add test for malformed UTF-8 sequences
void test_text_message_serialization_invalid_utf8()
{
    const uint8_t invalid_utf8[] = {0xFF, 0xFE, 0xFD, 0x00}; // Invalid UTF-8
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, invalid_utf8, sizeof(invalid_utf8) - 1);

    // Should not crash, may produce replacement characters
    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);
}
