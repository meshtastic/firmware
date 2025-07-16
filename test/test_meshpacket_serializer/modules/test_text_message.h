#pragma once
#include "test_helpers.h"

// Test TEXT_MESSAGE_APP port serialization
void test_text_message_serialization()
{
    const char *message = "Hello, Mesh!";

    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, (const uint8_t *)message, strlen(message));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Check message type
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("text", jsonObj["type"]->AsString().c_str());

    // Check payload
    TEST_ASSERT_TRUE(jsonObj.find("payload") != jsonObj.end());
    TEST_ASSERT_TRUE(jsonObj["payload"]->IsObject());

    JSONObject payload = jsonObj["payload"]->AsObject();
    TEST_ASSERT_TRUE(payload.find("text") != payload.end());
    TEST_ASSERT_EQUAL_STRING(message, payload["text"]->AsString().c_str());

    delete root;
}
