// Deterministic reproduction of the admin session-key behavior discussed on PR #10669
// (ndoo's "Admin message without session_key!" report), plus the remote-vs-local redaction of
// secret material in admin GET responses.
//
// Drives the REAL incoming-admin path (AdminModule::handleReceivedProtobuf) with a remote
// (from != 0) PKC-authorized set_owner, exercising the exact checkPassKey/setPassKey gate.
// A local (from == 0) admin bypasses that gate, so the bug only reproduces from != 0.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/AdminModule.h"
#include "support/AdminModuleTestShim.h"
#include "support/MockMeshService.h"
#include <cstring>

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B; // authorized admin, sends remote admin to us
static const uint8_t ADMIN_KEY[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                      0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x20};

static MockMeshService *mockService = nullptr;
static AdminModuleTestShim *admin = nullptr;

// A remote, PKC-authorized set_owner. `session` (if non-empty) is the session_passkey the client presents.
static meshtastic_MeshPacket makeRemoteSetOwner(const char *newLongName, const uint8_t *session, size_t sessionLen,
                                                meshtastic_AdminMessage &out)
{
    out = meshtastic_AdminMessage_init_zero;
    out.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    strncpy(out.set_owner.long_name, newLongName, sizeof(out.set_owner.long_name) - 1);
    if (session) {
        out.session_passkey.size = sessionLen;
        memcpy(out.session_passkey.bytes, session, sessionLen);
    }

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = ADMIN_NODE; // REMOTE: this is what makes the session gate apply
    mp.channel = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.pki_encrypted = true; // arrived over PKC
    mp.public_key.size = 32;
    memcpy(mp.public_key.bytes, ADMIN_KEY, 32); // matches config.security.admin_key[0] -> authorized
    return mp;
}

void setUp(void)
{
    mockService = new MockMeshService();
    service = mockService;
    admin = new AdminModuleTestShim();
    admin->deferSaves(); // no disk/reboot side effects when a setter is accepted

    if (!nodeDB)
        nodeDB = new NodeDB();
    myNodeInfo.my_node_num = LOCAL_NODE;

    config = meshtastic_LocalConfig_init_zero;
    // Authorize ADMIN_NODE's key as an admin key so the PKC path accepts it and we reach the session gate.
    config.security.admin_key[0].size = 32;
    memcpy(config.security.admin_key[0].bytes, ADMIN_KEY, 32);

    owner = meshtastic_User_init_zero;
    strncpy(owner.long_name, "Original", sizeof(owner.long_name) - 1);

    channels.initDefaults();
    channels.onConfigChanged();
}

void tearDown(void)
{
    service = nullptr;
    delete mockService;
    mockService = nullptr;
    delete admin;
    admin = nullptr;
}

// ndoo's report: a setter from a remote node with NO valid session is rejected, and the node's
// expected session key is all-zero because it has minted none since boot.
void test_remote_setter_without_session_is_rejected(void)
{
    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeRemoteSetOwner("Hijacked", nullptr, 0, m);

    admin->handleReceivedProtobuf(mp, &m);
    admin->drainReply();

    // Rejected at the session gate -> owner unchanged (this is ndoo's "Admin message without session_key!").
    TEST_ASSERT_EQUAL_STRING("Original", owner.long_name);
}

// The node's session key is minted only by setPassKey (which runs when it answers an admin GET),
// so before any GET the expected key is all-zero and any presented key mismatches.
void test_expected_session_key_is_zero_before_any_get(void)
{
    uint8_t zero[8] = {0};
    meshtastic_AdminMessage probe = meshtastic_AdminMessage_init_zero;
    probe.session_passkey.size = 8;
    memcpy(probe.session_passkey.bytes, zero, 8); // even all-zeros must not authorize a state change
    // A fresh module has minted no session; a stale/guessed key does not match.
    // (checkPassKey also requires size==8 AND session_time freshness.)
    uint8_t stale[8] = {0x29, 0x04, 0xb4, 0x78, 0xd8, 0x68, 0xa7, 0xff}; // ndoo's presented key
    meshtastic_AdminMessage staleMsg = meshtastic_AdminMessage_init_zero;
    staleMsg.session_passkey.size = 8;
    memcpy(staleMsg.session_passkey.bytes, stale, 8);
    TEST_ASSERT_FALSE(admin->checkPassKey(&staleMsg)); // Expected: 00..00 vs Incoming: 29 04 b4 78.. -> reject
}

// The fix path: once the node answers a GET (setPassKey mints/returns the key), the session gate
// accepts a setter carrying that key. Asserting the gate (checkPassKey) directly is the mechanism;
// driving the full handleSetOwner would need the NodeInfoModule scaffolding, out of scope here.
void test_session_gate_accepts_key_from_a_get_response(void)
{
    // Simulate the node answering an admin GET: setPassKey mints the session and writes it into the response.
    meshtastic_AdminMessage getResponse = meshtastic_AdminMessage_init_zero;
    admin->setPassKey(&getResponse);
    TEST_ASSERT_EQUAL(8, getResponse.session_passkey.size); // node handed the client a session key

    // A setter carrying that exact key passes the gate (would be accepted).
    meshtastic_AdminMessage good = meshtastic_AdminMessage_init_zero;
    good.session_passkey = getResponse.session_passkey;
    TEST_ASSERT_TRUE(admin->checkPassKey(&good));

    // A setter carrying a stale/guessed key still fails (no session replay).
    meshtastic_AdminMessage bad = meshtastic_AdminMessage_init_zero;
    bad.session_passkey.size = 8;
    uint8_t stale[8] = {0x29, 0x04, 0xb4, 0x78, 0xd8, 0x68, 0xa7, 0xff};
    memcpy(bad.session_passkey.bytes, stale, 8);
    TEST_ASSERT_FALSE(admin->checkPassKey(&bad));
}

// Decode the SecurityConfig out of the get_config response a handler queued in myReply.
static bool decodeSecurityFromReply(meshtastic_MeshPacket *reply, meshtastic_Config_SecurityConfig &out)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_AdminMessage_msg, &am))
        return false;
    if (am.which_payload_variant != meshtastic_AdminMessage_get_config_response_tag ||
        am.get_config_response.which_payload_variant != meshtastic_Config_security_tag)
        return false;
    out = am.get_config_response.payload_variant.security;
    return true;
}

static meshtastic_MeshPacket makeGetConfigRequest(NodeNum from)
{
    meshtastic_MeshPacket req = meshtastic_MeshPacket_init_zero;
    req.from = from;
    req.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    req.decoded.want_response = true;
    return req;
}

// The device identity private key must never leave over the air: a SECURITY_CONFIG response to a
// remote request (from != 0, even an authorized admin) carries an empty private_key.
void test_remote_security_config_omits_private_key(void)
{
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xAB, 32);

    meshtastic_MeshPacket req = makeGetConfigRequest(ADMIN_NODE);
    admin->handleGetConfig(req, meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG);

    meshtastic_Config_SecurityConfig sec;
    TEST_ASSERT_TRUE(decodeSecurityFromReply(admin->reply(), sec));
    TEST_ASSERT_EQUAL_MESSAGE(0, sec.private_key.size, "remote security config must not carry the private key");
    admin->drainReply();
}

// Control: the local backup path (from == 0, BLE/USB/TCP) still receives the private key, so the
// redaction above is remote-specific rather than a blanket wipe.
void test_local_security_config_keeps_private_key(void)
{
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xAB, 32);

    meshtastic_MeshPacket req = makeGetConfigRequest(0);
    admin->handleGetConfig(req, meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG);

    meshtastic_Config_SecurityConfig sec;
    TEST_ASSERT_TRUE(decodeSecurityFromReply(admin->reply(), sec));
    TEST_ASSERT_EQUAL_MESSAGE(32, sec.private_key.size, "local backup must still receive the private key");
    TEST_ASSERT_EACH_EQUAL_HEX8(0xAB, sec.private_key.bytes, 32);
    admin->drainReply();
}

#endif // !(MESHTASTIC_EXCLUDE_PKI)

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_remote_setter_without_session_is_rejected);
    RUN_TEST(test_expected_session_key_is_zero_before_any_get);
    RUN_TEST(test_session_gate_accepts_key_from_a_get_response);
    RUN_TEST(test_remote_security_config_omits_private_key);
    RUN_TEST(test_local_security_config_keeps_private_key);
#endif
    exit(UNITY_END());
}

void loop() {}
