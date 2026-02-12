#include "../test_helpers.h"

static size_t encode_traceroute(uint8_t *buffer, size_t buffer_size)
{
    meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
    route.route_count = 1;
    route.route[0] = 0x12345678;
    route.snr_towards_count = 1;
    route.snr_towards[0] = 4;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    pb_encode(&stream, &meshtastic_RouteDiscovery_msg, &route);
    return stream.bytes_written;
}

void test_traceroute_request_serialization()
{
    uint8_t buffer[256];
    size_t payload_size = encode_traceroute(buffer, sizeof(buffer));

    meshtastic_MeshPacket packet = create_test_packet(meshtastic_PortNum_TRACEROUTE_APP, buffer, payload_size);
    packet.decoded.request_id = 0;

    std::string json = MeshPacketSerializer::JsonSerialize(&packet, false);
    TEST_ASSERT_TRUE(json.length() > 0);

    JSONValue *root = JSON::Parse(json.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(root->IsObject());

    JSONObject jsonObj = root->AsObject();

    TEST_ASSERT_TRUE(jsonObj.find("type") != jsonObj.end());
    TEST_ASSERT_EQUAL_STRING("traceroute", jsonObj["type"]->AsString().c_str());
    TEST_ASSERT_TRUE(jsonObj.find("payload") == jsonObj.end());

    delete root;
}
