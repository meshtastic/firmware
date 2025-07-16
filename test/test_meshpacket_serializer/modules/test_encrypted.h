#pragma once
#include "test_helpers.h"

// Test encrypted packet serialization (packet that cannot be deserialized)
void test_encrypted_packet_serialization()
{
    // Create a packet that looks encrypted (random data that won't decode)
    uint8_t encrypted_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.id = 12345;
    packet.from = 0xAABBCCDD;
    packet.to = 0xFFFFFFFF;
    packet.channel = 0;
    packet.hop_limit = 3;
    packet.want_ack = false;
    packet.priority = meshtastic_MeshPacket_Priority_UNSET;
    packet.rx_time = 1609459200;
    packet.rx_snr = 10.5f;
    packet.hop_start = 3;
    packet.rx_rssi = -85;
    packet.delayed = meshtastic_MeshPacket_Delayed_NO_DELAY;

    // Set encrypted variant
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    memcpy(packet.encrypted.bytes, encrypted_data, sizeof(encrypted_data));
    packet.encrypted.size = sizeof(encrypted_data);

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    // Should have empty type for encrypted/undecryptable packets
    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("", jsonObj["type"]->AsString().c_str());

    delete root;
}
