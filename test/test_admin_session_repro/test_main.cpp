// Deterministic reproduction of the admin session-key behavior discussed on PR #10669
// (ndoo's "Admin message without session_key!" report), plus the remote-vs-local redaction of
// secret material in admin GET responses, plus the request/response pairing that decides which
// admin responses are accepted at all.
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

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B;   // authorized admin, sends remote admin to us
static constexpr NodeNum QUERIED_NODE = 0x0C0C0C0C; // a remote we send admin requests to
static constexpr NodeNum STRANGER_NODE = 0x0D0D0D0D;
static const uint8_t ADMIN_KEY[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                      0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x20};

// MeshService assigns every outgoing packet an id before noteOutgoingAdminRequest sees it, and
// setReplyTo echoes it back as decoded.request_id, so the pairing is keyed on it.
static constexpr uint32_t REQUEST_ID = 0x5EED0001;
static const uint8_t QUERIED_KEY[32] = {0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCa, 0xCb,
                                        0xCc, 0xCd, 0xCe, 0xCf, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
                                        0xD7, 0xD8, 0xD9, 0xDa, 0xDb, 0xDc, 0xDd, 0xDe, 0xDf, 0xE0};

// NodeDB with injectable nodes, so a destination can have a stored public key to pin.
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    void addNodeWithKey(NodeNum num, const uint8_t *key)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        if (key) {
            n.public_key.size = 32;
            memcpy(n.public_key.bytes, key, 32);
        }
        testNodes.push_back(n);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockMeshService *mockService = nullptr;
static AdminModuleTestShim *admin = nullptr;
static MockNodeDB *mockNodeDB = nullptr;

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

// A get_module_config_response carrying a remote_hardware pin list, as a remote would answer.
// This is the class of message that short-circuited auth: no session passkey, sender need not
// hold an admin key. handleGetModuleConfigResponse() stamps mp.from into the pin table.
static meshtastic_MeshPacket makeModuleConfigResponse(NodeNum from, meshtastic_AdminMessage &out)
{
    out = meshtastic_AdminMessage_init_zero;
    out.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
    out.get_module_config_response.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
    out.get_module_config_response.payload_variant.remote_hardware.enabled = true;
    out.get_module_config_response.payload_variant.remote_hardware.available_pins_count = 1;
    out.get_module_config_response.payload_variant.remote_hardware.available_pins[0].gpio_pin = 17;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = LOCAL_NODE;
    mp.channel = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.request_id = REQUEST_ID; // setReplyTo echoes the request's packet id
    return mp;
}

// One known pin entry, so a response that reaches handleGetModuleConfigResponse rewrites its owner.
static void seedRemoteHardwarePin(NodeNum owner)
{
    devicestate.node_remote_hardware_pins_count = 1;
    devicestate.node_remote_hardware_pins[0] = meshtastic_NodeRemoteHardwarePin_init_zero;
    devicestate.node_remote_hardware_pins[0].node_num = owner;
    devicestate.node_remote_hardware_pins[0].has_pin = true;
    devicestate.node_remote_hardware_pins[0].pin.gpio_pin = 4;
}

// The outgoing request `req` a local client would send to `to`, as MeshService sees it (from == 0,
// ADMIN_APP, payload still plaintext).
static meshtastic_MeshPacket makeOutgoingRequest(NodeNum to, const meshtastic_AdminMessage &req)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = 0;
    p.to = to;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    p.id = REQUEST_ID;
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_AdminMessage_msg, &req);
    return p;
}

static meshtastic_MeshPacket makeOutgoingModuleConfigRequest(
    NodeNum to, meshtastic_AdminMessage_ModuleConfigType type = meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG)
{
    meshtastic_AdminMessage req = meshtastic_AdminMessage_init_zero;
    req.which_payload_variant = meshtastic_AdminMessage_get_module_config_request_tag;
    req.get_module_config_request = type;
    return makeOutgoingRequest(to, req);
}

void setUp(void)
{
    mockService = new MockMeshService();
    service = mockService;
    admin = new AdminModuleTestShim();
    admin->deferSaves(); // no disk/reboot side effects when a setter is accepted

    if (!mockNodeDB)
        mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();
    nodeDB = mockNodeDB;
    myNodeInfo.my_node_num = LOCAL_NODE;

#ifdef ARCH_PORTDUINO
    // The native test harness boots Portduino in simulated mode, and wouldEncryptWithPKC()
    // hard-disables PKC whenever force_simradio is set. Left true, no outgoing admin request is
    // ever key-pinned, so the pinning tests below cannot exercise what they are asserting.
    // Model a real (non-sim) device instead.
    portduino_config.force_simradio = false;
#endif

    config = meshtastic_LocalConfig_init_zero;
    // A real device always holds a private key; without one perhapsEncode never picks PKC.
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xA5, 32);
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

// Decode the NetworkConfig / MqttConfig out of the response a handler queued in myReply.
static bool decodeNetworkFromReply(meshtastic_MeshPacket *reply, meshtastic_Config_NetworkConfig &out)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_AdminMessage_msg, &am))
        return false;
    if (am.which_payload_variant != meshtastic_AdminMessage_get_config_response_tag ||
        am.get_config_response.which_payload_variant != meshtastic_Config_network_tag)
        return false;
    out = am.get_config_response.payload_variant.network;
    return true;
}

static bool decodeMqttFromReply(meshtastic_MeshPacket *reply, meshtastic_ModuleConfig_MQTTConfig &out)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_AdminMessage_msg, &am))
        return false;
    if (am.which_payload_variant != meshtastic_AdminMessage_get_module_config_response_tag ||
        am.get_module_config_response.which_payload_variant != meshtastic_ModuleConfig_mqtt_tag)
        return false;
    out = am.get_module_config_response.payload_variant.mqtt;
    return true;
}

// A remote requester gets the sentinel; the set path swaps the stored value back.
void test_remote_network_config_omits_wifi_psk(void)
{
    strcpy(config.network.wifi_psk, "hunter2hunter2");

    meshtastic_MeshPacket req = makeGetConfigRequest(ADMIN_NODE);
    admin->handleGetConfig(req, meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG);

    meshtastic_Config_NetworkConfig net;
    TEST_ASSERT_TRUE(decodeNetworkFromReply(admin->reply(), net));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sekrit", net.wifi_psk, "remote network config must not carry the real psk");
    admin->drainReply();
}

// Control: the local path still receives the stored psk.
void test_local_network_config_keeps_wifi_psk(void)
{
    strcpy(config.network.wifi_psk, "hunter2hunter2");

    meshtastic_MeshPacket req = makeGetConfigRequest(0);
    admin->handleGetConfig(req, meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG);

    meshtastic_Config_NetworkConfig net;
    TEST_ASSERT_TRUE(decodeNetworkFromReply(admin->reply(), net));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hunter2hunter2", net.wifi_psk, "local client must still receive the psk");
    admin->drainReply();
}

void test_remote_mqtt_config_omits_password(void)
{
    strcpy(moduleConfig.mqtt.password, "brokerpass");

    meshtastic_MeshPacket req = makeGetConfigRequest(ADMIN_NODE);
    admin->handleGetModuleConfig(req, meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG);

    meshtastic_ModuleConfig_MQTTConfig mqtt;
    TEST_ASSERT_TRUE(decodeMqttFromReply(admin->reply(), mqtt));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sekrit", mqtt.password, "remote mqtt config must not carry the broker password");
    admin->drainReply();
}

void test_local_mqtt_config_keeps_password(void)
{
    strcpy(moduleConfig.mqtt.password, "brokerpass");

    meshtastic_MeshPacket req = makeGetConfigRequest(0);
    admin->handleGetModuleConfig(req, meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG);

    meshtastic_ModuleConfig_MQTTConfig mqtt;
    TEST_ASSERT_TRUE(decodeMqttFromReply(admin->reply(), mqtt));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("brokerpass", mqtt.password, "local client must still receive the password");
    admin->drainReply();
}

// A client that GETs and writes the config straight back must not wipe the stored value.
void test_set_config_sentinel_psk_preserves_stored_value(void)
{
    strcpy(config.network.wifi_psk, "hunter2hunter2");

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_network_tag;
    c.payload_variant.network = config.network;
    strcpy(c.payload_variant.network.wifi_psk, "sekrit");

    admin->deferSaves();
    admin->handleSetConfig(c, true);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("hunter2hunter2", config.network.wifi_psk,
                                     "a read-modify-write round trip must not wipe the psk");
    admin->drainReply();
}

// An admin response carries no session passkey and its sender is not an admin-key holder, so a
// request we sent is the only thing vouching for it. A get_module_config_response from a node we
// never queried is not.
static constexpr pb_size_t MODULE_CONFIG_RESPONSE = meshtastic_AdminMessage_get_module_config_response_tag;
static constexpr pb_size_t REMOTE_HW_TAG = meshtastic_ModuleConfig_remote_hardware_tag; // makeModuleConfigResponse subtype

void test_unsolicited_response_is_not_solicited(void)
{
    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);

    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a response nobody asked for must not be accepted");
}

// Control: once the client has sent that node a request, its response is accepted. Without this,
// the test above would also pass if responseIsSolicited() simply always said no.
void test_response_after_our_request_is_solicited(void)
{
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(STRANGER_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);

    TEST_ASSERT_TRUE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                             "the answer to our own request must be accepted");
}

// A request to one remote does not vouch for a different remote's response.
void test_request_to_one_node_does_not_admit_another(void)
{
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(QUERIED_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m); // answered by someone else

    TEST_ASSERT_FALSE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG));
}

// A response only answers its own request type: a get_owner_request does not admit a
// get_module_config_response from the same node.
void test_response_variant_must_match_request(void)
{
    meshtastic_AdminMessage owner_req = meshtastic_AdminMessage_init_zero;
    owner_req.which_payload_variant = meshtastic_AdminMessage_get_owner_request_tag;
    admin->noteOutgoingAdminRequest(makeOutgoingRequest(STRANGER_NODE, owner_req));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m); // wrong type for the request

    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a get_owner request must not admit a get_module_config response");
    // ...but the response it actually asked for is still accepted from the same slot.
    TEST_ASSERT_TRUE(admin->responseIsSolicited(mp, meshtastic_AdminMessage_get_owner_response_tag, 0));
}

// Regression: each request keeps its own pinned key. A later unpinned request to the same node
// must not relax the PKC pin of an earlier one (the old shared-slot model cleared it).
void test_pinned_request_keeps_its_key_after_an_unpinned_request(void)
{
    // QUERIED has a stored key, so a request to it will be PKC-encrypted and pins that key.
    // STRANGER has none, so a request to it cannot be pinned.
    mockNodeDB->addNodeWithKey(QUERIED_NODE, QUERIED_KEY);
    mockNodeDB->addNodeWithKey(STRANGER_NODE, nullptr);

    meshtastic_AdminMessage cfg = meshtastic_AdminMessage_init_zero;
    cfg.which_payload_variant = meshtastic_AdminMessage_get_config_request_tag;
    admin->noteOutgoingAdminRequest(makeOutgoingRequest(QUERIED_NODE, cfg));

    meshtastic_AdminMessage own = meshtastic_AdminMessage_init_zero;
    own.which_payload_variant = meshtastic_AdminMessage_get_owner_request_tag;
    admin->noteOutgoingAdminRequest(makeOutgoingRequest(STRANGER_NODE, own));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket resp = makeModuleConfigResponse(QUERIED_NODE, m); // pki off by default

    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(resp, meshtastic_AdminMessage_get_config_response_tag, 0),
                              "an unpinned request must not relax an earlier request's key pin");

    resp.pki_encrypted = true;
    resp.public_key.size = 32;
    memcpy(resp.public_key.bytes, QUERIED_KEY, 32);
    TEST_ASSERT_TRUE(admin->responseIsSolicited(resp, meshtastic_AdminMessage_get_config_response_tag, 0));
}

// The pin is taken from the destination's stored NodeDB key - the key perhapsEncode will encrypt
// to. Reading the outgoing packet's public_key instead pinned nothing: nothing populates that
// field before encryption, so every real request was unpinned and `from` alone admitted responses.
void test_request_to_keyed_node_pins_the_stored_key(void)
{
    mockNodeDB->addNodeWithKey(QUERIED_NODE, QUERIED_KEY);
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(QUERIED_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket plain = makeModuleConfigResponse(QUERIED_NODE, m);
    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(plain, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a plaintext response must not answer a PKC-pinned request");

    meshtastic_MeshPacket wrongKey = makeModuleConfigResponse(QUERIED_NODE, m);
    wrongKey.pki_encrypted = true;
    wrongKey.public_key.size = 32;
    memset(wrongKey.public_key.bytes, 0x77, 32);
    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(wrongKey, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a response under a different key must not be accepted");

    meshtastic_MeshPacket good = makeModuleConfigResponse(QUERIED_NODE, m);
    good.pki_encrypted = true;
    good.public_key.size = 32;
    memcpy(good.public_key.bytes, QUERIED_KEY, 32);
    TEST_ASSERT_TRUE_MESSAGE(admin->responseIsSolicited(good, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                             "the pinned key must still admit the genuine response");
}

// Ham mode never uses PKC, so pinning a key there would reject the legitimate plaintext response.
void test_ham_mode_request_is_not_pinned(void)
{
    owner.is_licensed = true;
    mockNodeDB->addNodeWithKey(QUERIED_NODE, QUERIED_KEY);
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(QUERIED_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(QUERIED_NODE, m);
    TEST_ASSERT_TRUE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                             "a request that could not have gone out over PKC must not be pinned");
}

// The response must echo our request's packet id, so an injector cannot answer a request it did
// not see just by naming the right node and variant.
void test_response_with_wrong_request_id_is_rejected(void)
{
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(STRANGER_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);
    mp.decoded.request_id = REQUEST_ID ^ 0xFFFF; // answers some other request

    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a response that does not echo our request id must be rejected");

    mp.decoded.request_id = REQUEST_ID;
    TEST_ASSERT_TRUE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                             "the matching request id must still be accepted");
}

// A request we could not bind to an id must not be answerable by a response that simply omits
// request_id, which decodes to 0.
void test_request_without_an_id_admits_nothing(void)
{
    meshtastic_MeshPacket req = makeOutgoingModuleConfigRequest(STRANGER_NODE);
    req.id = 0;
    admin->noteOutgoingAdminRequest(req);

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);
    mp.decoded.request_id = 0;

    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a zero request id must not act as a matching token");
}

// A remote_hardware response must answer a request for that exact subtype, not just any module
// config - else an MQTT-config request could authorize a pin-table update.
void test_module_config_subtype_must_match(void)
{
    admin->noteOutgoingAdminRequest(
        makeOutgoingModuleConfigRequest(STRANGER_NODE, meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m); // remote_hardware subtype
    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a non-remote-hardware request must not admit a remote_hardware response");

    // Control: a request for the matching subtype does admit it.
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(STRANGER_NODE));
    TEST_ASSERT_TRUE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG));
}

// A matched request is consumed, so a node cannot replay a state-mutating response within the window.
void test_response_is_consumed_no_replay(void)
{
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(STRANGER_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);
    TEST_ASSERT_TRUE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG));
    TEST_ASSERT_FALSE_MESSAGE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG),
                              "a second copy of an already-answered response must be rejected");
}

// Only requests arm the gate: sending a setter to a node must not make it a trusted responder.
void test_outgoing_setter_does_not_admit_responses(void)
{
    meshtastic_AdminMessage setter = meshtastic_AdminMessage_init_zero;
    setter.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    admin->noteOutgoingAdminRequest(makeOutgoingRequest(STRANGER_NODE, setter));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);

    TEST_ASSERT_FALSE(admin->responseIsSolicited(mp, MODULE_CONFIG_RESPONSE, REMOTE_HW_TAG));
}

// End to end through the real handler: an unsolicited get_module_config_response must not reach
// handleGetModuleConfigResponse, so the remote_hardware pin table keeps its owner.
void test_unsolicited_response_does_not_poison_pins(void)
{
    seedRemoteHardwarePin(QUERIED_NODE);

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);
    admin->handleReceivedProtobuf(mp, &m);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(QUERIED_NODE, devicestate.node_remote_hardware_pins[0].node_num,
                                     "unsolicited response must not rewrite the pin owner");
}

// Control: once we have requested it, the same response is handled and updates the pin table. This
// proves the assertion above is the auth gate firing, not the handler being dead. (It also exercises
// the dispatch tag fixed in this change - without it, the handler never runs for either node.)
void test_solicited_response_updates_pins(void)
{
    seedRemoteHardwarePin(QUERIED_NODE);
    admin->noteOutgoingAdminRequest(makeOutgoingModuleConfigRequest(STRANGER_NODE));

    meshtastic_AdminMessage m;
    meshtastic_MeshPacket mp = makeModuleConfigResponse(STRANGER_NODE, m);
    admin->handleReceivedProtobuf(mp, &m);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(STRANGER_NODE, devicestate.node_remote_hardware_pins[0].node_num,
                                     "a response we asked for must reach the handler");
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
    RUN_TEST(test_remote_network_config_omits_wifi_psk);
    RUN_TEST(test_local_network_config_keeps_wifi_psk);
    RUN_TEST(test_remote_mqtt_config_omits_password);
    RUN_TEST(test_local_mqtt_config_keeps_password);
    RUN_TEST(test_set_config_sentinel_psk_preserves_stored_value);
    RUN_TEST(test_unsolicited_response_is_not_solicited);
    RUN_TEST(test_response_after_our_request_is_solicited);
    RUN_TEST(test_request_to_one_node_does_not_admit_another);
    RUN_TEST(test_response_variant_must_match_request);
    RUN_TEST(test_pinned_request_keeps_its_key_after_an_unpinned_request);
    RUN_TEST(test_request_to_keyed_node_pins_the_stored_key);
    RUN_TEST(test_ham_mode_request_is_not_pinned);
    RUN_TEST(test_response_with_wrong_request_id_is_rejected);
    RUN_TEST(test_request_without_an_id_admits_nothing);
    RUN_TEST(test_module_config_subtype_must_match);
    RUN_TEST(test_response_is_consumed_no_replay);
    RUN_TEST(test_outgoing_setter_does_not_admit_responses);
    RUN_TEST(test_unsolicited_response_does_not_poison_pins);
    RUN_TEST(test_solicited_response_updates_pins);
#endif
    exit(UNITY_END());
}

void loop() {}
