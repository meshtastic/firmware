// Tests for KeyVerificationModule's refusal to run in licensed (ham) mode.
//
// Pinned: a licensed node starts no key verification session - neither one the user asks for
// (sendInitialRequest, reached from the admin INITIATE_VERIFICATION message and the screen menu)
// nor one a remote node requests (allocReply). The user is told why; the remote gets no reply and
// no NAK, as against a busy node.
//
// Why: the handshake's third message must be PKI-encrypted, and licensed operation neither sends
// nor decrypts PKI. Before this guard a licensed node opened the session, raised the security-number
// prompt, and then waited for a message that could never arrive until the session timed out.
//
// The refusal has to come before the module touches NodeDB, the router or the RNG. This suite runs
// with all three unset, so a regression that lets either path proceed fails here - as a crash on the
// first dereference if not an assertion - rather than passing silently.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/KeyVerificationModule.h"
#include "support/MockMeshService.h"

namespace
{
constexpr NodeNum REMOTE_NODE = 0x22222222;

// Exposes the reply hook and the request slot MeshModule::callModules() would normally fill.
class TestKeyVerificationModule : public KeyVerificationModule
{
  public:
    using KeyVerificationModule::allocReply;
    using MeshModule::currentRequest;
    bool requestIgnored() const { return ignoreRequest; }
};

TestKeyVerificationModule *module;
MockMeshService *mockService;
} // namespace

void setUp(void)
{
    owner = meshtastic_User_init_zero;
    owner.is_licensed = true;
    nodeDB = nullptr;
    router = nullptr;
    mockService = new MockMeshService();
    service = mockService;
    module = new TestKeyVerificationModule();
}

void tearDown(void)
{
    TestKeyVerificationModule::currentRequest = nullptr;
    delete module;
    module = nullptr;
    delete mockService;
    mockService = nullptr;
    service = nullptr;
    owner = meshtastic_User_init_zero;
}

// The user's request (admin message or screen menu) is refused, and the client is told why.
void test_licensed_initiate_is_refused_and_reported(void)
{
    TEST_ASSERT_FALSE_MESSAGE(module->sendInitialRequest(REMOTE_NODE), "a licensed node must not start key verification");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockService->notificationCount, "the client must be told why nothing happened");
}

// A remote node's request opens no session and gets no reply or NAK.
void test_licensed_incoming_request_gets_no_reply(void)
{
    meshtastic_MeshPacket request = meshtastic_MeshPacket_init_zero;
    request.from = REMOTE_NODE;
    request.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    request.decoded.portnum = meshtastic_PortNum_KEY_VERIFICATION_APP;
    request.decoded.want_response = true;
    TestKeyVerificationModule::currentRequest = &request;

    TEST_ASSERT_NULL_MESSAGE(module->allocReply(), "a licensed node must not answer a key verification request");
    TEST_ASSERT_TRUE_MESSAGE(module->requestIgnored(), "the refusal must not NAK the requester");
    TEST_ASSERT_EQUAL(0, mockService->notificationCount);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_licensed_initiate_is_refused_and_reported);
    RUN_TEST(test_licensed_incoming_request_gets_no_reply);
    exit(UNITY_END());
}

#else

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif

void loop() {}
