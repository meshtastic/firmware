#include "../test_helpers.h"

// Test TEXT_MESSAGE_APP port
void test_text_message_serialization()
{
    const char *test_text = "Hello Meshtastic!";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, (const uint8_t *)test_text, strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
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

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("text", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();
    TEST_ASSERT_TRUE(payload.find("text") != payload.end());
    TEST_ASSERT_EQUAL_STRING("Hello Meshtastic!", payload["text"]->AsString().c_str());

    delete root;
}
