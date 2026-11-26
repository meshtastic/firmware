// The KeyVerificationModule three-message handshake, both roles in one process.
//
// Under test: sendInitialRequest(), allocReply(), handleReceivedProtobuf(), the admin
// PROVIDE_SECURITY_NUMBER/DO_VERIFY path and updateState() in src/modules/KeyVerificationModule.cpp.
// Why: the security number is the MitM check; a mismatch that still reaches AWAITING_USER would
// mark an unverified key as manually verified and persist it.
// Regression guarded: processSecurityNumber() accepting a hash mismatch, and a stale session that
// never returns to IDLE.
#include "MeshTypes.h" // before TestUtil.h
#include "TestUtil.h"
#include <cstring>
#include <pb_decode.h>
#include <unity.h>
#include <vector>

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "UptimeClock.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "modules/KeyVerificationModule.h"
#include "support/MockMeshService.h"

namespace
{
constexpr NodeNum NODE_A = 0x11111111;
constexpr NodeNum NODE_B = 0x22222222;

class CaptureRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t, bool = false) override { return 0; }
};

// Capture the packet so the test carries it by hand. Like the real TX queue, keep it alive until
// tearDown: sendToMesh() still reads it after sendLocal() returns, to copy it to the phone.
class CaptureRouter : public Router
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packets.push_back(*p);
        held.push_back(p);
        return ERRNO_OK;
    }
    void releaseHeld()
    {
        for (auto *p : held)
            packetPool.release(p);
        held.clear();
    }
    std::vector<meshtastic_MeshPacket> packets;
    std::vector<meshtastic_MeshPacket *> held;
};

class CaptureService : public MockMeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override
    {
        notes.push_back(*n);
        MockMeshService::sendClientNotification(n);
    }
    std::vector<meshtastic_ClientNotification> notes;
};

class TestKeyVerification : public KeyVerificationModule
{
  public:
    using KeyVerificationModule::allocReply;
    using KeyVerificationModule::handleAdminMessageForModule;
    using KeyVerificationModule::handleReceivedProtobuf;
    using MeshModule::currentRequest;
};

CaptureRouter *testRouter;
CaptureService *serviceMock;
NodeDB *testNodeDB;
TestKeyVerification *moduleA;
TestKeyVerification *moduleB;
meshtastic_User ownerA, ownerB;

void switchTo(NodeNum node, const meshtastic_User &own)
{
    myNodeInfo.my_node_num = node;
    owner = own;
}

void installKey(NodeNum node, const meshtastic_User &u)
{
    meshtastic_NodeInfoLite *n = testNodeDB->getOrCreateMeshNode(node);
    TEST_ASSERT_NOT_NULL(n);
    n->public_key.size = u.public_key.size;
    memcpy(n->public_key.bytes, u.public_key.bytes, u.public_key.size);
}

void startHandshake()
{
    testRouter->packets.clear();
    switchTo(NODE_A, ownerA);
    TEST_ASSERT_TRUE(moduleA->sendInitialRequest(NODE_B));
    TEST_ASSERT_EQUAL(1, testRouter->packets.size());
}

meshtastic_KeyVerification decode(const meshtastic_MeshPacket &p)
{
    meshtastic_KeyVerification kv = meshtastic_KeyVerification_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_KeyVerification_msg, &kv));
    return kv;
}

// A's init -> B's reply -> A sees hash2. Returns A's nonce; B's security number is in its notification.
uint64_t exchangeToNumberPicker(uint32_t &securityNumber)
{
    startHandshake();
    meshtastic_MeshPacket m1 = testRouter->packets.back();

    switchTo(NODE_B, ownerB);
    moduleB->currentRequest = &m1;
    serviceMock->notes.clear();
    meshtastic_MeshPacket *reply = moduleB->allocReply();
    TEST_ASSERT_NOT_NULL(reply);
    meshtastic_MeshPacket m2 = *reply;
    packetPool.release(reply);
    m2.from = NODE_B;
    m2.to = NODE_A;

    bool haveNumber = false;
    for (const auto &n : serviceMock->notes) {
        if (n.which_payload_variant == meshtastic_ClientNotification_key_verification_number_inform_tag) {
            securityNumber = n.payload_variant.key_verification_number_inform.security_number;
            haveNumber = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(haveNumber, "the responder must tell its user the security number");

    switchTo(NODE_A, ownerA);
    serviceMock->notes.clear();
    meshtastic_KeyVerification kv2 = decode(m2);
    TEST_ASSERT_TRUE(moduleA->handleReceivedProtobuf(m2, &kv2));

    uint64_t nonce = 0;
    for (const auto &n : serviceMock->notes) {
        if (n.which_payload_variant == meshtastic_ClientNotification_key_verification_number_request_tag) {
            nonce = n.payload_variant.key_verification_number_request.nonce;
        }
    }
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, nonce, "the initiator must ask its user for the number");
    return nonce;
}

bool admin(TestKeyVerification *module, meshtastic_KeyVerificationAdmin_MessageType type, uint64_t nonce, uint32_t number = 0)
{
    meshtastic_AdminMessage req = meshtastic_AdminMessage_init_zero;
    req.which_payload_variant = meshtastic_AdminMessage_key_verification_tag;
    req.key_verification.message_type = type;
    req.key_verification.nonce = nonce;
    if (type == meshtastic_KeyVerificationAdmin_MessageType_PROVIDE_SECURITY_NUMBER) {
        req.key_verification.has_security_number = true;
        req.key_verification.security_number = number;
    }
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshtastic_AdminMessage resp = meshtastic_AdminMessage_init_zero;
    return module->handleAdminMessageForModule(mp, &req, &resp) == AdminMessageHandleResult::HANDLED;
}

bool verified(NodeNum node)
{
    meshtastic_NodeInfoLite *n = testNodeDB->getMeshNode(node);
    return n && (n->bitfield & NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK);
}
} // namespace

void setUp(void)
{
    owner = meshtastic_User_init_zero;
    owner.is_licensed = false;
    testNodeDB = new NodeDB();
    nodeDB = testNodeDB;
    testRouter = new CaptureRouter();
    testRouter->addInterface(std::unique_ptr<RadioInterface>(new CaptureRadio()));
    router = testRouter;
    serviceMock = new CaptureService();
    service = serviceMock;
    moduleA = new TestKeyVerification();
    moduleB = new TestKeyVerification();
    keyVerificationModule = moduleA;

    ownerA = meshtastic_User_init_zero;
    ownerB = meshtastic_User_init_zero;
    ownerA.is_licensed = ownerB.is_licensed = false;
    uint8_t pubA[32], privA[32], pubB[32], privB[32];
    crypto->generateKeyPair(pubA, privA);
    crypto->generateKeyPair(pubB, privB);
    ownerA.public_key.size = ownerB.public_key.size = 32;
    memcpy(ownerA.public_key.bytes, pubA, 32);
    memcpy(ownerB.public_key.bytes, pubB, 32);
    installKey(NODE_A, ownerA);
    installKey(NODE_B, ownerB);
    switchTo(NODE_A, ownerA);
}

void tearDown(void)
{
    Time::useRealClock();
    keyVerificationModule = nullptr;
    delete moduleA;
    delete moduleB;
    moduleA = moduleB = nullptr;
    while (auto *toPhone = serviceMock->getForPhone()) // sendToMesh's copies for the phone
        serviceMock->releaseToPool(toPhone);
    service = nullptr;
    delete serviceMock;
    serviceMock = nullptr;
    router = nullptr;
    testRouter->releaseHeld();
    delete testRouter;
    testRouter = nullptr;
    nodeDB = nullptr;
    delete testNodeDB;
    testNodeDB = nullptr;
    owner = meshtastic_User_init_zero;
}

void test_handshake_right_number_marks_both_sides_verified(void)
{
    uint32_t number = 0;
    const uint64_t nonce = exchangeToNumberPicker(number);

    switchTo(NODE_A, ownerA);
    testRouter->packets.clear();
    admin(moduleA, meshtastic_KeyVerificationAdmin_MessageType_PROVIDE_SECURITY_NUMBER, nonce, number);
    TEST_ASSERT_EQUAL(1, testRouter->packets.size());
    meshtastic_MeshPacket m3 = testRouter->packets.back();
    m3.from = NODE_A;
    m3.to = NODE_B;
    TEST_ASSERT_TRUE_MESSAGE(m3.pki_encrypted, "the responder only accepts the hash over PKI");

    switchTo(NODE_B, ownerB);
    meshtastic_KeyVerification kv3 = decode(m3);
    TEST_ASSERT_TRUE(moduleB->handleReceivedProtobuf(m3, &kv3));

    switchTo(NODE_A, ownerA);
    TEST_ASSERT_TRUE(admin(moduleA, meshtastic_KeyVerificationAdmin_MessageType_DO_VERIFY, nonce));
    TEST_ASSERT_TRUE(verified(NODE_B));

    switchTo(NODE_B, ownerB);
    TEST_ASSERT_TRUE(admin(moduleB, meshtastic_KeyVerificationAdmin_MessageType_DO_VERIFY, nonce));
    TEST_ASSERT_TRUE(verified(NODE_A));
}

void test_handshake_wrong_number_does_not_verify(void)
{
    uint32_t number = 0;
    const uint64_t nonce = exchangeToNumberPicker(number);

    switchTo(NODE_A, ownerA);
    testRouter->packets.clear();
    admin(moduleA, meshtastic_KeyVerificationAdmin_MessageType_PROVIDE_SECURITY_NUMBER, nonce, number + 1);
    TEST_ASSERT_EQUAL(0, testRouter->packets.size());

    TEST_ASSERT_TRUE(admin(moduleA, meshtastic_KeyVerificationAdmin_MessageType_DO_VERIFY, nonce));
    TEST_ASSERT_FALSE_MESSAGE(verified(NODE_B), "a wrong security number must never commit the key");
}

void test_handshake_timeout_returns_to_idle(void)
{
    Time::setTestMillis(100000); // sessionStartedMs and the timeout check both read this clock
    startHandshake();

    switchTo(NODE_A, ownerA);
    TEST_ASSERT_FALSE_MESSAGE(moduleA->sendInitialRequest(NODE_B), "a live session must refuse a second request");

    // The hard cap is millis()-based; 61 s only exercises the RTC-seconds path, which native getTime()
    // does not necessarily follow.
    Time::advanceTestMillis(180000 + 1000);
    TEST_ASSERT_TRUE_MESSAGE(moduleA->sendInitialRequest(NODE_B), "an expired session must return to IDLE and allow a new one");
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_handshake_right_number_marks_both_sides_verified);
    RUN_TEST(test_handshake_wrong_number_does_not_verify);
    RUN_TEST(test_handshake_timeout_returns_to_idle);
    exit(UNITY_END());
}

void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
