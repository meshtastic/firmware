#include "DebugConfiguration.h"
#include "MeshService.h"
#include "TestUtil.h"
#include <unity.h>

namespace
{

struct DummyClientToken {
    uint8_t value = 0;
};

static PhoneAPI *asClient(DummyClientToken &token)
{
    return reinterpret_cast<PhoneAPI *>(&token);
}

static meshtastic_MeshPacket *allocDecodedPacket(uint32_t id, NodeNum to = 0)
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    TEST_ASSERT_NOT_NULL(p);
    p->id = id;
    p->to = to;
    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    return p;
}

void test_fanout_packet_delivered_to_all_active_clients_once(void)
{
    MeshService meshService;
    DummyClientToken c1, c2, c3;
    PhoneAPI *client1 = asClient(c1);
    PhoneAPI *client2 = asClient(c2);
    PhoneAPI *client3 = asClient(c3);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(client1, MeshService::STATE_SERIAL));
    TEST_ASSERT_TRUE(meshService.registerPhoneClient(client2, MeshService::STATE_BLE));
    TEST_ASSERT_TRUE(meshService.registerPhoneClient(client3, MeshService::STATE_WIFI));

    meshService.sendToPhone(allocDecodedPacket(101, 0x1001));

    meshtastic_MeshPacket *p1 = meshService.getForPhone(client1);
    meshtastic_MeshPacket *p2 = meshService.getForPhone(client2);
    meshtastic_MeshPacket *p3 = meshService.getForPhone(client3);

    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_NOT_NULL(p3);
    TEST_ASSERT_EQUAL_UINT32(101, p1->id);
    TEST_ASSERT_EQUAL_UINT32(101, p2->id);
    TEST_ASSERT_EQUAL_UINT32(101, p3->id);

    meshService.releaseToPoolForPhone(client1, p1);
    meshService.releaseToPoolForPhone(client2, p2);
    meshService.releaseToPoolForPhone(client3, p3);

    TEST_ASSERT_NULL(meshService.getForPhone(client1));
    TEST_ASSERT_NULL(meshService.getForPhone(client2));
    TEST_ASSERT_NULL(meshService.getForPhone(client3));
}

void test_slow_client_drop_oldest_fast_client_continuous_delivery(void)
{
    MeshService meshService;
    DummyClientToken fastToken, slowToken;
    PhoneAPI *fastClient = asClient(fastToken);
    PhoneAPI *slowClient = asClient(slowToken);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(fastClient, MeshService::STATE_SERIAL));
    TEST_ASSERT_TRUE(meshService.registerPhoneClient(slowClient, MeshService::STATE_WIFI));

    const uint32_t totalPackets = MAX_RX_TOPHONE + 4;
    for (uint32_t i = 1; i <= totalPackets; i++) {
        meshService.sendToPhone(allocDecodedPacket(i, i));

        meshtastic_MeshPacket *fastPacket = meshService.getForPhone(fastClient);
        TEST_ASSERT_NOT_NULL(fastPacket);
        TEST_ASSERT_EQUAL_UINT32(i, fastPacket->id);
        meshService.releaseToPoolForPhone(fastClient, fastPacket);
    }

    const uint32_t firstExpected = totalPackets - MAX_RX_TOPHONE + 1;
    for (uint32_t expectedId = firstExpected; expectedId <= totalPackets; expectedId++) {
        meshtastic_MeshPacket *slowPacket = meshService.getForPhone(slowClient);
        TEST_ASSERT_NOT_NULL(slowPacket);
        TEST_ASSERT_EQUAL_UINT32(expectedId, slowPacket->id);
        meshService.releaseToPoolForPhone(slowClient, slowPacket);
    }

    TEST_ASSERT_NULL(meshService.getForPhone(slowClient));
}

void test_disconnect_cleans_pending_and_inflight_without_breaking_other_clients(void)
{
    MeshService meshService;
    DummyClientToken firstToken, secondToken;
    PhoneAPI *firstClient = asClient(firstToken);
    PhoneAPI *secondClient = asClient(secondToken);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(firstClient, MeshService::STATE_BLE));
    TEST_ASSERT_TRUE(meshService.registerPhoneClient(secondClient, MeshService::STATE_WIFI));

    meshService.sendToPhone(allocDecodedPacket(201, 0x2001));
    meshtastic_MeshPacket *firstInflight = meshService.getForPhone(firstClient);
    TEST_ASSERT_NOT_NULL(firstInflight);
    TEST_ASSERT_EQUAL_UINT32(201, firstInflight->id);

    meshService.unregisterPhoneClient(firstClient);

    meshtastic_MeshPacket *secondPacket = meshService.getForPhone(secondClient);
    TEST_ASSERT_NOT_NULL(secondPacket);
    TEST_ASSERT_EQUAL_UINT32(201, secondPacket->id);
    meshService.releaseToPoolForPhone(secondClient, secondPacket);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(firstClient, MeshService::STATE_BLE));
    meshService.sendToPhone(allocDecodedPacket(202, 0x2002));
    meshService.unregisterPhoneClient(firstClient); // pending packet for firstClient is dropped on unregister

    meshtastic_MeshPacket *secondPacket2 = meshService.getForPhone(secondClient);
    TEST_ASSERT_NOT_NULL(secondPacket2);
    TEST_ASSERT_EQUAL_UINT32(202, secondPacket2->id);
    meshService.releaseToPoolForPhone(secondClient, secondPacket2);
}

void test_no_active_clients_does_not_buffer_packets(void)
{
    MeshService meshService;
    meshService.sendToPhone(allocDecodedPacket(301, 0x3001));

    DummyClientToken token;
    PhoneAPI *client = asClient(token);
    TEST_ASSERT_TRUE(meshService.registerPhoneClient(client, MeshService::STATE_SERIAL));
    TEST_ASSERT_NULL(meshService.getForPhone(client));
}

void test_api_state_mask_refcount_for_same_state_clients(void)
{
    MeshService meshService;
    DummyClientToken firstToken, secondToken;
    PhoneAPI *firstClient = asClient(firstToken);
    PhoneAPI *secondClient = asClient(secondToken);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(firstClient, MeshService::STATE_SERIAL));
    TEST_ASSERT_TRUE((meshService.api_state_mask & MeshService::apiStateBit(MeshService::STATE_SERIAL)) != 0u);

    TEST_ASSERT_TRUE(meshService.registerPhoneClient(secondClient, MeshService::STATE_SERIAL));
    TEST_ASSERT_TRUE((meshService.api_state_mask & MeshService::apiStateBit(MeshService::STATE_SERIAL)) != 0u);

    meshService.unregisterPhoneClient(firstClient);
    TEST_ASSERT_TRUE((meshService.api_state_mask & MeshService::apiStateBit(MeshService::STATE_SERIAL)) != 0u);

    meshService.unregisterPhoneClient(secondClient);
    TEST_ASSERT_EQUAL_UINT32(0u, meshService.api_state_mask);
    TEST_ASSERT_EQUAL(MeshService::STATE_DISCONNECTED, meshService.api_state);
}

void test_fanout_pool_capacity_accounts_for_inflight_all_clients(void)
{
    MeshService meshService;
    DummyClientToken tokens[MAX_PHONE_API_CLIENTS];
    PhoneAPI *clients[MAX_PHONE_API_CLIENTS] = {};
    meshtastic_MeshPacket *heldInflight[MAX_PHONE_API_CLIENTS] = {};

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        clients[i] = asClient(tokens[i]);
        TEST_ASSERT_TRUE(meshService.registerPhoneClient(clients[i], MeshService::STATE_SERIAL));
    }

    for (uint32_t i = 1; i <= MAX_RX_TOPHONE; i++) {
        meshService.sendToPhone(allocDecodedPacket(4000 + i, 0x4000 + i));
    }

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        heldInflight[i] = meshService.getForPhone(clients[i]);
        TEST_ASSERT_NOT_NULL(heldInflight[i]);
    }

    const uint32_t extraPacketId = 4999;
    meshService.sendToPhone(allocDecodedPacket(extraPacketId, 0x4999));

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        meshService.releaseToPoolForPhone(clients[i], heldInflight[i]);
        heldInflight[i] = nullptr;

        bool sawExtraPacket = false;
        for (int q = 0; q < MAX_RX_TOPHONE; q++) {
            meshtastic_MeshPacket *p = meshService.getForPhone(clients[i]);
            TEST_ASSERT_NOT_NULL(p);
            if (p->id == extraPacketId) {
                sawExtraPacket = true;
            }
            meshService.releaseToPoolForPhone(clients[i], p);
            if (sawExtraPacket)
                break;
        }
        TEST_ASSERT_TRUE(sawExtraPacket);
    }
}

void test_register_overflow_does_not_change_state_mask_or_existing_delivery(void)
{
    MeshService meshService;
    DummyClientToken tokens[MAX_PHONE_API_CLIENTS];
    PhoneAPI *clients[MAX_PHONE_API_CLIENTS] = {};

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        clients[i] = asClient(tokens[i]);
        MeshService::APIState state = (i % 2 == 0) ? MeshService::STATE_SERIAL : MeshService::STATE_WIFI;
        TEST_ASSERT_TRUE(meshService.registerPhoneClient(clients[i], state));
    }

    uint32_t expectedMask = MeshService::apiStateBit(MeshService::STATE_SERIAL) | MeshService::apiStateBit(MeshService::STATE_WIFI);
    TEST_ASSERT_EQUAL_UINT32(expectedMask, meshService.api_state_mask);

    DummyClientToken overflowToken;
    PhoneAPI *overflowClient = asClient(overflowToken);
    TEST_ASSERT_FALSE(meshService.registerPhoneClient(overflowClient, MeshService::STATE_BLE));
    TEST_ASSERT_EQUAL_UINT32(expectedMask, meshService.api_state_mask);

    meshService.sendToPhone(allocDecodedPacket(5001, 0x5001));

    for (int i = 0; i < MAX_PHONE_API_CLIENTS; i++) {
        meshtastic_MeshPacket *p = meshService.getForPhone(clients[i]);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT32(5001, p->id);
        meshService.releaseToPoolForPhone(clients[i], p);
    }

    TEST_ASSERT_NULL(meshService.getForPhone(overflowClient));
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_fanout_packet_delivered_to_all_active_clients_once);
    RUN_TEST(test_slow_client_drop_oldest_fast_client_continuous_delivery);
    RUN_TEST(test_disconnect_cleans_pending_and_inflight_without_breaking_other_clients);
    RUN_TEST(test_no_active_clients_does_not_buffer_packets);
    RUN_TEST(test_api_state_mask_refcount_for_same_state_clients);
    RUN_TEST(test_fanout_pool_capacity_accounts_for_inflight_all_clients);
    RUN_TEST(test_register_overflow_does_not_change_state_mask_or_existing_delivery);
    exit(UNITY_END());
}

void loop() {}
