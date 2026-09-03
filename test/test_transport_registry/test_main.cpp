// Unit tests for MeshTransportBase, the non-LoRa broadcast transport registry that Router::send()
// fans outgoing packets out to (UDP multicast, BLE mesh). These pin the invariant the refactor could
// silently break: callTransports() must reach every enabled transport, in registration order, and a
// transport accepting a packet must never suppress a later one (parallel media, not a STOP chain).
// The wrapped handlers' own internals stay covered by test_mqtt and test_ble_mesh.

#include "TestUtil.h"
#include "mesh/MeshTransportBase.h"
#include <unity.h>
#include <vector>

namespace
{

// Order in which transports were asked to send, recorded across every mock in a single test.
std::vector<int> callLog;

class MockTransport : public MeshTransportBase
{
  public:
    explicit MockTransport(int id, bool enabled = true, bool sendResult = true) : id(id), enabled(enabled), sendResult(sendResult)
    {
    }

    int id;
    bool enabled;
    bool sendResult;
    int sends = 0;
    const meshtastic_MeshPacket *lastPacket = nullptr;

    bool isEnabled() const override { return enabled; }

    bool onSend(const meshtastic_MeshPacket *mp) override
    {
        sends++;
        lastPacket = mp;
        callLog.push_back(id);
        return sendResult;
    }
};

meshtastic_MeshPacket samplePacket()
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = 0x1234abcd;
    p.id = 0x0000beef;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = 8;
    return p;
}

} // namespace

void setUp(void)
{
    callLog.clear();
}
void tearDown(void) {}

// Two enabled transports both receive the exact packet, in registration (construction) order.
void test_all_enabled_transports_are_called_in_order(void)
{
    MockTransport a(1);
    MockTransport b(2);
    auto p = samplePacket();

    MeshTransportBase::callTransports(&p);

    TEST_ASSERT_EQUAL_INT(1, a.sends);
    TEST_ASSERT_EQUAL_INT(1, b.sends);
    TEST_ASSERT_EQUAL_PTR(&p, a.lastPacket);
    TEST_ASSERT_EQUAL_PTR(&p, b.lastPacket);
    TEST_ASSERT_EQUAL_INT(2, (int)callLog.size());
    TEST_ASSERT_EQUAL_INT(1, callLog[0]);
    TEST_ASSERT_EQUAL_INT(2, callLog[1]);
}

// The first transport returning true must NOT suppress a later one - there is no STOP contract.
void test_return_value_does_not_suppress_later_transports(void)
{
    MockTransport a(1, /*enabled=*/true, /*sendResult=*/true);
    MockTransport b(2, /*enabled=*/true, /*sendResult=*/true);
    auto p = samplePacket();

    MeshTransportBase::callTransports(&p);

    TEST_ASSERT_EQUAL_INT(1, a.sends);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, b.sends, "a returning true must not stop b");
}

// A disabled transport is skipped; enabled ones still fire.
void test_disabled_transport_is_skipped(void)
{
    MockTransport a(1, /*enabled=*/false);
    MockTransport b(2, /*enabled=*/true);
    auto p = samplePacket();

    MeshTransportBase::callTransports(&p);

    TEST_ASSERT_EQUAL_INT(0, a.sends);
    TEST_ASSERT_EQUAL_INT(1, b.sends);
    TEST_ASSERT_EQUAL_INT(1, (int)callLog.size());
    TEST_ASSERT_EQUAL_INT(2, callLog[0]);
}

// With nothing registered (all mocks destructed), callTransports is a safe no-op.
void test_empty_registry_is_a_noop(void)
{
    auto p = samplePacket();
    MeshTransportBase::callTransports(&p); // must not crash
    TEST_ASSERT_EQUAL_INT(0, (int)callLog.size());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_all_enabled_transports_are_called_in_order);
    RUN_TEST(test_return_value_does_not_suppress_later_transports);
    RUN_TEST(test_disabled_transport_is_skipped);
    RUN_TEST(test_empty_registry_is_a_noop);
    exit(UNITY_END());
}

void loop() {}
