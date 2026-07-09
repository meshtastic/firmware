// Tests for the admin-key fallback in Router::perhapsDecode.
//
// When a PKI/PKC-encrypted unicast DM arrives addressed to us from a sender we have NO public key for,
// perhapsDecode falls back to trying each configured admin_key as a candidate sender key. On success it
// (a) decodes the packet and sets p->public_key to the admin key that worked, and (b) persists that key
// into NodeDB for the sender (getOrCreateMeshNode), so future packets take the fast path and we can
// PKI-reply. decryptCurve25519 is AES-CCM AEAD, so a wrong candidate key reliably fails authentication.
//
// These drive the real crypto + NodeDB path: we mint an admin keypair, encrypt a packet exactly as the
// admin's radio would (DH against our public key), then hand it to perhapsDecode and inspect the result.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

// The whole feature is compiled out when PKI is excluded.
#if !(MESHTASTIC_EXCLUDE_PKI)

#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h" // MESHTASTIC_PKC_OVERHEAD
#include "mesh/Router.h"
#include <cstring>
#include <pb_encode.h>
#include <vector>

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A; // us (the receiver)
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B; // an authorized admin, absent from our NodeDB
static constexpr uint32_t PKT_ID = 0x12345678;

// ---------------------------------------------------------------------------
// MockNodeDB - inject nodes with controlled public keys. Mirrors test/test_packet_signing.
// meshNodes/numMeshNodes are public on NodeDB.
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    void addNode(NodeNum num)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    void setPublicKey(NodeNum num, const uint8_t *pubKey)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        n->public_key.size = 32;
        memcpy(n->public_key.bytes, pubKey, 32);
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockNodeDB *mockNodeDB = nullptr;

// Keypairs, regenerated fresh each test in setUp(). "our" == the receiver, "admin" == the sender.
static uint8_t ourPub[32], ourPriv[32];
static uint8_t adminPub[32], adminPriv[32];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Store a 32-byte key into config.security.admin_key[slot].
static void setAdminKey(int slot, const uint8_t *key32)
{
    config.security.admin_key[slot].size = 32;
    memcpy(config.security.admin_key[slot].bytes, key32, 32);
    if (slot + 1 > (int)config.security.admin_key_count)
        config.security.admin_key_count = slot + 1;
}

// Build a PKI/PKC-encrypted unicast packet FROM `from` TO us, encrypted with `senderPriv` against our
// public key - i.e. exactly what the admin's radio puts on the air. Leaves the CryptoEngine holding our
// private key afterwards (as it would be during normal receive) so perhapsDecode can decrypt.
static meshtastic_MeshPacket makePkiPacket(NodeNum from, meshtastic_PortNum port, size_t payloadLen, const uint8_t *senderPriv)
{
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = port;
    data.payload.size = payloadLen;
    for (size_t i = 0; i < payloadLen; i++)
        data.payload.bytes[i] = (uint8_t)(i & 0xff);

    uint8_t plain[meshtastic_Constants_DATA_PAYLOAD_LEN];
    size_t plainLen = pb_encode_to_bytes(plain, sizeof(plain), &meshtastic_Data_msg, &data);
    TEST_ASSERT_TRUE_MESSAGE(plainLen > 0, "pb_encode_to_bytes failed in test setup");

    meshtastic_NodeInfoLite_public_key_t ourPubStruct;
    ourPubStruct.size = 32;
    memcpy(ourPubStruct.bytes, ourPub, 32);

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = LOCAL_NODE;
    p.id = PKT_ID;
    p.channel = 0; // PKI packets carry channel hash 0
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    // Encrypt AS the sender: shared secret = DH(senderPriv, ourPub).
    crypto->setDHPrivateKey(const_cast<uint8_t *>(senderPriv));
    bool ok = crypto->encryptCurve25519(p.to, p.from, ourPubStruct, p.id, plainLen, plain, p.encrypted.bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "encryptCurve25519 failed in test setup");
    p.encrypted.size = plainLen + MESHTASTIC_PKC_OVERHEAD;

    // Restore the engine to our private key, as it is when receiving.
    crypto->setDHPrivateKey(ourPriv);
    return p;
}

// Assert the packet decoded via PKI and that we learned `expectedKey` for its sender.
static void assertDecodedAndLearned(meshtastic_MeshPacket *p, const uint8_t *expectedKey)
{
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_decoded_tag, p->which_payload_variant);
    TEST_ASSERT_TRUE(p->pki_encrypted);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_PRIVATE_APP, p->decoded.portnum);
    TEST_ASSERT_EQUAL(32, p->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(expectedKey, p->public_key.bytes, 32);

    meshtastic_NodeInfoLite *learned = mockNodeDB->getMeshNode(p->from);
    TEST_ASSERT_NOT_NULL_MESSAGE(learned, "sender should have been created in NodeDB");
    TEST_ASSERT_EQUAL_MESSAGE(32, learned->public_key.size, "sender key should have been persisted");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expectedKey, learned->public_key.bytes, 32, "persisted key mismatch");
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------
void setUp(void)
{
    // Construct the mock FIRST: the NodeDB ctor can reload persisted host state and repopulate globals.
    mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();
    nodeDB = mockNodeDB;

    config = meshtastic_LocalConfig_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE; // drives isToUs()/getFrom()

    channels.initDefaults();
    channels.onConfigChanged();

    // Fresh keypairs for us and the admin (independent, valid Curve25519 pairs).
    crypto->generateKeyPair(ourPub, ourPriv);
    crypto->generateKeyPair(adminPub, adminPriv);

    // perhapsDecode's PKI gate requires that we have our own key (getMeshNode(p->to)->public_key).
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, ourPub);

    // During receive the engine holds our private key.
    crypto->setDHPrivateKey(ourPriv);
}

void tearDown(void)
{
    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Admin key in slot 0: a DM from an unknown sender decrypts via the fallback, and the key is persisted.
void test_admin_key_slot0_decrypts_and_persists(void)
{
    setAdminKey(0, adminPub);
    TEST_ASSERT_NULL_MESSAGE(mockNodeDB->getMeshNode(ADMIN_NODE), "precondition: sender is unknown to us");

    meshtastic_MeshPacket p = makePkiPacket(ADMIN_NODE, meshtastic_PortNum_PRIVATE_APP, 16, adminPriv);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));

    assertDecodedAndLearned(&p, adminPub);
}

// The loop scans every admin slot, not just [0]: a key provisioned only in slot 2 still works.
void test_admin_key_slot2_only_decrypts(void)
{
    setAdminKey(2, adminPub); // slots 0 and 1 left empty

    meshtastic_MeshPacket p = makePkiPacket(ADMIN_NODE, meshtastic_PortNum_PRIVATE_APP, 16, adminPriv);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));

    assertDecodedAndLearned(&p, adminPub);
}

// No admin key configured + unknown sender: nothing decrypts, and we must NOT invent a key for anyone.
void test_no_admin_key_unknown_sender_not_decoded(void)
{
    // config (incl. admin_key) is zeroed by setUp(); ADMIN_NODE is absent from NodeDB.
    meshtastic_MeshPacket p = makePkiPacket(ADMIN_NODE, meshtastic_PortNum_PRIVATE_APP, 16, adminPriv);

    TEST_ASSERT_NOT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));
    TEST_ASSERT_NOT_EQUAL(meshtastic_MeshPacket_decoded_tag, p.which_payload_variant);
    TEST_ASSERT_NULL_MESSAGE(mockNodeDB->getMeshNode(ADMIN_NODE), "must not learn a key when nothing decrypted");
}

// A configured admin key that is NOT the sender's must fail authentication (no false-positive decode,
// no bogus key learned).
void test_wrong_admin_key_does_not_decode(void)
{
    uint8_t otherPub[32], otherPriv[32];
    crypto->generateKeyPair(otherPub, otherPriv); // unrelated key
    crypto->setDHPrivateKey(ourPriv);             // restore receive key (generateKeyPair changed it)
    setAdminKey(0, otherPub);                     // admin slot holds a key that did NOT encrypt the packet

    meshtastic_MeshPacket p = makePkiPacket(ADMIN_NODE, meshtastic_PortNum_PRIVATE_APP, 16, adminPriv);

    TEST_ASSERT_NOT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));
    TEST_ASSERT_NOT_EQUAL(meshtastic_MeshPacket_decoded_tag, p.which_payload_variant);
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(ADMIN_NODE));
}

#endif // !(MESHTASTIC_EXCLUDE_PKI)

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN();
#if !(MESHTASTIC_EXCLUDE_PKI)
    RUN_TEST(test_admin_key_slot0_decrypts_and_persists);
    RUN_TEST(test_admin_key_slot2_only_decrypts);
    RUN_TEST(test_no_admin_key_unknown_sender_not_decoded);
    RUN_TEST(test_wrong_admin_key_does_not_decode);
#endif
    exit(UNITY_END());
}

void loop() {}
