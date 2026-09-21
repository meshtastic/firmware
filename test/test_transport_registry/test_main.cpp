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

// Separate log for the pre-encode hook, kept out of callLog so the post-encode tests' size assertions
// stay byte-identical.
std::vector<int> preCallLog;

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

// A transport shaped like the MQTTTransport adapter: registers at the PreEncode point, so it must be
// reached only by callTransportsPreEncode and never by the post-encode callTransports. isEnabled()
// returns true on purpose - it would matter only on the post path, which must never reach a PreEncode
// transport regardless.
class PreMockTransport : public MeshTransportBase
{
  public:
    explicit PreMockTransport(int id) : MeshTransportBase(MeshTransportBase::PreEncode), id(id) {}

    int id;
    int preSends = 0;
    int postSends = 0; // must stay 0: a PreEncode transport is never in the post fan-out
    const meshtastic_MeshPacket *lastEncrypted = nullptr;
    const meshtastic_MeshPacket *lastDecoded = nullptr;
    ChannelIndex lastChIndex = 0xFF;

    bool isEnabled() const override { return true; }
    bool onSend(const meshtastic_MeshPacket *) override
    {
        postSends++;
        return true;
    }
    void onSendPreEncode(const meshtastic_MeshPacket &enc, const meshtastic_MeshPacket &dec, ChannelIndex ch) override
    {
        preSends++;
        lastEncrypted = &enc;
        lastDecoded = &dec;
        lastChIndex = ch;
        preCallLog.push_back(id);
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

meshtastic_MeshPacket sampleDecodedPacket()
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = 0x1234abcd;
    p.id = 0x0000beef;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    return p;
}

} // namespace

void setUp(void)
{
    callLog.clear();
    preCallLog.clear();
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

// Every PreEncode transport receives the encrypted packet, the decoded copy and the channel index, in
// registration order.
void test_pre_encode_transports_receive_decoded_and_encrypted(void)
{
    PreMockTransport a(10);
    PreMockTransport b(11);
    auto enc = samplePacket();
    auto dec = sampleDecodedPacket();

    MeshTransportBase::callTransportsPreEncode(enc, dec, /*chIndex=*/3);

    TEST_ASSERT_EQUAL_INT(1, a.preSends);
    TEST_ASSERT_EQUAL_INT(1, b.preSends);
    TEST_ASSERT_EQUAL_PTR(&enc, a.lastEncrypted);
    TEST_ASSERT_EQUAL_PTR(&dec, a.lastDecoded);
    TEST_ASSERT_EQUAL_UINT8(3, a.lastChIndex);
    TEST_ASSERT_EQUAL_INT(2, (int)preCallLog.size());
    TEST_ASSERT_EQUAL_INT(10, preCallLog[0]);
    TEST_ASSERT_EQUAL_INT(11, preCallLog[1]);
}

// The two fan-out points are disjoint. This is the MQTT invariant expressed at the registry level: MQTT
// registers at the pre point (reached only for our own originations, which the Router call site gates
// with moduleConfig.mqtt.enabled && isFromUs) and is never in the post fan-out, so it can never publish
// the relayed / already-encrypted broadcast traffic that flows through callTransports().
void test_pre_and_post_hooks_are_disjoint(void)
{
    PreMockTransport pre(10); // shaped like the MQTTTransport adapter
    MockTransport post(1);    // shaped like UDP / BLE
    auto p = samplePacket();

    MeshTransportBase::callTransports(&p);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, post.sends, "post fan-out must reach the PostEncode transport");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pre.preSends, "post fan-out must NOT reach the pre hook");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pre.postSends, "a PreEncode transport must never be in the post fan-out");
    TEST_ASSERT_EQUAL_INT(1, (int)callLog.size());
    TEST_ASSERT_EQUAL_INT(0, (int)preCallLog.size());

    auto dec = sampleDecodedPacket();
    MeshTransportBase::callTransportsPreEncode(p, dec, /*chIndex=*/0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, pre.preSends, "pre fan-out must reach the pre hook");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, post.sends, "pre fan-out must NOT re-invoke the PostEncode transport");
    TEST_ASSERT_EQUAL_INT(1, (int)callLog.size());
    TEST_ASSERT_EQUAL_INT(1, (int)preCallLog.size());
}

// With no PreEncode transport registered, callTransportsPreEncode is a safe no-op.
void test_empty_pre_encode_registry_is_a_noop(void)
{
    auto enc = samplePacket();
    auto dec = sampleDecodedPacket();
    MeshTransportBase::callTransportsPreEncode(enc, dec, /*chIndex=*/0); // must not crash
    TEST_ASSERT_EQUAL_INT(0, (int)preCallLog.size());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_all_enabled_transports_are_called_in_order);
    RUN_TEST(test_return_value_does_not_suppress_later_transports);
    RUN_TEST(test_disabled_transport_is_skipped);
    RUN_TEST(test_empty_registry_is_a_noop);
    RUN_TEST(test_pre_encode_transports_receive_decoded_and_encrypted);
    RUN_TEST(test_pre_and_post_hooks_are_disjoint);
    RUN_TEST(test_empty_pre_encode_registry_is_a_noop);
    exit(UNITY_END());
}

void loop() {}
