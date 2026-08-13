#include "../test_helpers.h"

void test_timestamp_present_when_has_rx_time()
{
    const char *test_text = "hi";
    meshtastic_MeshPacket packet =
        create_test_packet(meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(test_text), strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isMember("timestamp"));
    TEST_ASSERT_EQUAL_UINT32(1609459200u, root["timestamp"].asUInt());
}

void test_timestamp_zeroed_when_rx_time_absent()
{
    const char *test_text = "hi";
    meshtastic_MeshPacket packet = create_test_packet_no_rx_time(meshtastic_PortNum_TEXT_MESSAGE_APP,
                                                                 reinterpret_cast<const uint8_t *>(test_text), strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isMember("timestamp"));
    TEST_ASSERT_EQUAL_UINT32(0u, root["timestamp"].asUInt()); // must not leak the uptime placeholder
}

void test_encrypted_timestamp_zeroed_when_rx_time_absent()
{
    const char *test_text = "hi";
    meshtastic_MeshPacket packet = create_test_packet_no_rx_time(meshtastic_PortNum_TEXT_MESSAGE_APP,
                                                                 reinterpret_cast<const uint8_t *>(test_text), strlen(test_text));

    std::string json = MeshPacketSerializer::JsonSerializeEncrypted(&packet);
    Json::Value root = parse_json(json);
    TEST_ASSERT_TRUE(root.isMember("timestamp"));
    TEST_ASSERT_EQUAL_UINT32(0u, root["timestamp"].asUInt());
}
