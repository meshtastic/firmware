// Identity hygiene for the remote-identity commit paths in NodeDB: updateUser() key pinning and
// addFromContact() guards (a keyless contact must never erase a stored key - the #11432 regression).
#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define IH_TEST_ENTRY extern "C"
#else
#define IH_TEST_ENTRY
#endif

#include "FSCommon.h"
#include "SPILock.h"
#include "mesh/NodeDB.h"
#include "support/MockMeshService.h"
#include <cstdio>
#include <cstring>

// Subclass shim: the friend declaration in NodeDB.h grants access to the
// private state these tests must seed/reset (duplicateWarned latch, warm-tier
// demotion). Declared at global scope so it matches `friend class NodeDBTestShim`.
class NodeDBTestShim : public NodeDB
{
  public:
    void clearHot()
    {
        meshNodes->clear();
        numMeshNodes = 0;
    }

    // keySeed == 0 means "no stored key"; otherwise a deterministic 32-byte pattern.
    void push(NodeNum num, uint32_t lastHeard, uint8_t keySeed = 0, bool xeddsaSigned = false)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = lastHeard;
        nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        if (keySeed) {
            n.public_key.size = 32;
            memset(n.public_key.bytes, keySeed, 32);
            n.public_key.bytes[0] = 0x01; // never all-zero (all-zero == "no key")
        }
        if (xeddsaSigned)
            nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, true);
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();
    }

    // Index 0 is our own node; eviction scans treat it as self.
    void seedSelf() { push(0x0BADF00D, 0xFFFFFFFFu); }

    void resetDuplicateWarned() { duplicateWarned = false; }

#if WARM_NODE_COUNT > 0
    void runDemote() { demoteOldestHotNodesToWarm(); }
#endif
};

namespace
{

NodeDBTestShim *db = nullptr;
MockMeshService *mockService = nullptr;

meshtastic_User savedOwner;
meshtastic_LocalConfig savedConfig;

constexpr NodeNum kPeer = 0xE1000001;

// Same pattern as NodeDBTestShim::push so a "matching" user key really matches.
template <typename KeyT> void fillKey(KeyT &k, uint8_t seed)
{
    k.size = 32;
    memset(k.bytes, seed, 32);
    k.bytes[0] = 0x01;
}

meshtastic_User makeUser(const char *longName, const char *shortName, uint8_t keySeed = 0)
{
    meshtastic_User u = meshtastic_User_init_zero;
    strncpy(u.long_name, longName, sizeof(u.long_name) - 1);
    strncpy(u.short_name, shortName, sizeof(u.short_name) - 1);
    if (keySeed)
        fillKey(u.public_key, keySeed);
    return u;
}

meshtastic_SharedContact makeContact(NodeNum num, const char *longName, const char *shortName, uint8_t keySeed = 0)
{
    meshtastic_SharedContact c = meshtastic_SharedContact_init_zero;
    c.node_num = num;
    c.has_user = true;
    c.user = makeUser(longName, shortName, keySeed);
    return c;
}

void assertStoredKeyEquals(NodeNum num, uint8_t seed)
{
    const meshtastic_NodeInfoLite *info = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL(32, info->public_key.size);
    uint8_t expected[32];
    memset(expected, seed, 32);
    expected[0] = 0x01;
    TEST_ASSERT_EQUAL_MEMORY(expected, info->public_key.bytes, 32);
}

} // namespace

// --- addFromContact ---

// The #11432 regression: a stored 32-byte key plus a contact with has_user=true
// but no key must keep the stored key bit-for-bit while still merging the user
// fields (clients send add_contact before every DM, usually keyless).
static void test_contact_keyless_preserves_stored_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);

    db->addFromContact(makeContact(kPeer, "Alice", "AL"));

    assertStoredKeyEquals(kPeer, 0x42);
    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_EQUAL_STRING("Alice", info->long_name); // merge still applied
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(info));     // anti-eviction stamp for normal roles
}

// The guard blocks erasure, not update: a contact carrying a different valid
// 32-byte key replaces the stored one (the QR contact-sharing flow).
static void test_contact_new_key_updates_stored_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);

    db->addFromContact(makeContact(kPeer, "Alice", "AL", /*keySeed=*/0x77));

    assertStoredKeyEquals(kPeer, 0x77);
}

// A manually-verified pin refuses the ENTIRE update from a non-verified contact
// whose key mismatches - name and key both stay untouched.
static void test_contact_verified_pin_blocks_mismatched_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);
    nodeInfoLiteSetBit(db->getMeshNode(kPeer), NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true);

    db->addFromContact(makeContact(kPeer, "Mallory", "MA", /*keySeed=*/0x77));

    assertStoredKeyEquals(kPeer, 0x42);
    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_EQUAL_STRING("", info->long_name);   // refused wholesale, not just the key
    TEST_ASSERT_FALSE(nodeInfoLiteIsFavorite(info)); // returned before the favorite stamp
    TEST_ASSERT_TRUE(nodeInfoLiteIsKeyManuallyVerified(info));
}

// The verified pin also refuses a KEYLESS non-verified contact wholesale (a
// size mismatch is a key mismatch) - unlike the plain erasure guard below,
// which merges the user fields and only restores the key.
static void test_contact_verified_pin_blocks_keyless_unverified(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);
    nodeInfoLiteSetBit(db->getMeshNode(kPeer), NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true);

    db->addFromContact(makeContact(kPeer, "Alice", "AL")); // keyless, not verified

    assertStoredKeyEquals(kPeer, 0x42);
    TEST_ASSERT_EQUAL_STRING("", db->getMeshNode(kPeer)->long_name);
}

// A non-verified contact whose key MATCHES the verified pin may still update
// the user fields; the verified bit survives the merge.
static void test_contact_verified_pin_allows_matching_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);
    nodeInfoLiteSetBit(db->getMeshNode(kPeer), NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true);

    db->addFromContact(makeContact(kPeer, "Alice", "AL", /*keySeed=*/0x42));

    assertStoredKeyEquals(kPeer, 0x42);
    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_EQUAL_STRING("Alice", info->long_name);
    TEST_ASSERT_TRUE(nodeInfoLiteIsKeyManuallyVerified(info));
}

// contact.manually_verified sets the bit, and a later plain update (here via
// updateUser with the pinned key) must not clear it - CopyUserToNodeInfoLite
// only touches the user-derived bits.
static void test_contact_manually_verified_bit_survives_updates(void)
{
    meshtastic_SharedContact c = makeContact(kPeer, "Alice", "AL", /*keySeed=*/0x42);
    c.manually_verified = true;
    db->addFromContact(c);
    TEST_ASSERT_TRUE(nodeInfoLiteIsKeyManuallyVerified(db->getMeshNode(kPeer)));

    meshtastic_User u = makeUser("Alice2", "A2", /*keySeed=*/0x42);
    TEST_ASSERT_TRUE(db->updateUser(kPeer, u));

    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_EQUAL_STRING("Alice2", info->long_name);
    TEST_ASSERT_TRUE(nodeInfoLiteIsKeyManuallyVerified(info));
    assertStoredKeyEquals(kPeer, 0x42);
}

// should_ignore blocks the contact and drops its satellite data but keeps the
// stored public key: an ignored peer stays a verifiable identity.
static void test_contact_should_ignore_blocks_but_keeps_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);
    nodeInfoLiteSetBit(db->getMeshNode(kPeer), NODEINFO_BITFIELD_IS_FAVORITE_MASK, true);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    meshtastic_PositionLite pos = meshtastic_PositionLite_init_zero;
    pos.latitude_i = 123456789;
    db->nodePositions[kPeer] = pos;
    TEST_ASSERT_TRUE(db->hasNodePosition(kPeer));
#endif

    meshtastic_SharedContact c = makeContact(kPeer, "Blocked", "BL"); // keyless on purpose
    c.should_ignore = true;
    db->addFromContact(c);

    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(info));
    TEST_ASSERT_FALSE(nodeInfoLiteIsFavorite(info));
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    TEST_ASSERT_FALSE(db->hasNodePosition(kPeer));
#endif
    assertStoredKeyEquals(kPeer, 0x42); // key retained through the keyless ignore contact
}

// CLIENT_BASE must not auto-favorite (is_favorite has special meaning there);
// the anti-eviction protection is a heard-now stamp instead.
static void test_contact_client_base_stamps_heard_not_favorite(void)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;

    db->addFromContact(makeContact(kPeer, "Alice", "AL"));

    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_FALSE(nodeInfoLiteIsFavorite(info));
    // initializeTestEnvironment() set an NTP-quality RTC, so the stamp lands in last_heard.
    TEST_ASSERT_NOT_EQUAL(0, info->last_heard);
}

// A contact without a user payload must not merge fields or apply should_ignore to an
// existing node. (getOrCreateMeshNode still runs first, so an unknown num would be
// admitted as a blank row - that path is not covered here.)
static void test_contact_without_user_is_noop(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);
    const size_t countBefore = db->getNumMeshNodes();

    meshtastic_SharedContact c = meshtastic_SharedContact_init_zero;
    c.node_num = kPeer;
    c.has_user = false;
    c.should_ignore = true;
    db->addFromContact(c);

    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(info));
    assertStoredKeyEquals(kPeer, 0x42);
    TEST_ASSERT_EQUAL_UINT(countBefore, db->getNumMeshNodes()); // existing node: no new row admitted
}

// --- updateUser ---

#if !(MESHTASTIC_EXCLUDE_PKI)

// A pinned 32-byte key is immutable against a NodeInfo carrying a different key.
static void test_updateuser_pinned_key_blocks_mismatch(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);

    meshtastic_User u = makeUser("Mallory", "MA", /*keySeed=*/0x77);
    TEST_ASSERT_FALSE(db->updateUser(kPeer, u));

    assertStoredKeyEquals(kPeer, 0x42);
    TEST_ASSERT_EQUAL_STRING("", db->getMeshNode(kPeer)->long_name); // dropped wholesale
}

// ...and against a NodeInfo carrying NO key: unlike addFromContact, updateUser
// drops a keyless update for a pinned node entirely.
static void test_updateuser_keyless_nodeinfo_dropped_wholesale(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42);

    meshtastic_User u = makeUser("Alice", "AL");
    TEST_ASSERT_FALSE(db->updateUser(kPeer, u));

    assertStoredKeyEquals(kPeer, 0x42);
    TEST_ASSERT_EQUAL_STRING("", db->getMeshNode(kPeer)->long_name);
}

// First key for a node is accepted (TOFU) and the reach-channel is stamped.
static void test_updateuser_first_key_accepted(void)
{
    db->push(kPeer, 1000);

    meshtastic_User u = makeUser("Alice", "AL", /*keySeed=*/0x42);
    TEST_ASSERT_TRUE(db->updateUser(kPeer, u, /*channelIndex=*/3));

    assertStoredKeyEquals(kPeer, 0x42);
    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_EQUAL_STRING("Alice", info->long_name);
    TEST_ASSERT_EQUAL(3, info->channel);
}

// A remote node advertising OUR public key is refused with exactly one
// ClientNotification; the duplicateWarned latch silences the second attempt.
static void test_updateuser_own_key_advert_notifies_once(void)
{
    fillKey(owner.public_key, 0x5A);
    meshtastic_User u = makeUser("Evil twin", "ET", /*keySeed=*/0x5A);

    TEST_ASSERT_FALSE(db->updateUser(kPeer, u));
    TEST_ASSERT_EQUAL(1, mockService->notificationCount);

    TEST_ASSERT_FALSE(db->updateUser(kPeer, u));
    TEST_ASSERT_EQUAL(1, mockService->notificationCount); // latched
}

// user.id is always re-derived from the node number, whatever the payload claims.
static void test_updateuser_id_derived_from_nodenum(void)
{
    meshtastic_User u = makeUser("Alice", "AL", /*keySeed=*/0x42);
    strncpy(u.id, "!deadbeef", sizeof(u.id) - 1);

    TEST_ASSERT_TRUE(db->updateUser(kPeer, u));

    char expected[16];
    snprintf(expected, sizeof(expected), "!%08x", (unsigned)kPeer);
    TEST_ASSERT_EQUAL_STRING(expected, u.id);
}

// A known XEdDSA signer's identity only changes via a signed update - even a
// same-key name change arriving unsigned is refused.
static void test_updateuser_unsigned_update_refused_for_hot_signer(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42, /*xeddsaSigned=*/true);
    meshtastic_User u = makeUser("New name", "NN", /*keySeed=*/0x42);

    TEST_ASSERT_FALSE(db->updateUser(kPeer, u, 0, /*xeddsaSigned=*/false));
    TEST_ASSERT_EQUAL_STRING("", db->getMeshNode(kPeer)->long_name);

    TEST_ASSERT_TRUE(db->updateUser(kPeer, u, 0, /*xeddsaSigned=*/true)); // signed control
    TEST_ASSERT_EQUAL_STRING("New name", db->getMeshNode(kPeer)->long_name);
}

// The key pin outranks the signature: a signed update still cannot rotate a
// pinned key (rotation goes through commitRemoteKey's proven paths instead).
static void test_updateuser_signed_update_cannot_rotate_pinned_key(void)
{
    db->push(kPeer, 1000, /*keySeed=*/0x42, /*xeddsaSigned=*/true);

    meshtastic_User u = makeUser("Rotated", "RO", /*keySeed=*/0x77);
    TEST_ASSERT_FALSE(db->updateUser(kPeer, u, 0, /*xeddsaSigned=*/true));

    assertStoredKeyEquals(kPeer, 0x42);
}

#if WARM_NODE_COUNT > 0
// The signer gate runs BEFORE getOrCreateMeshNode, so refusing an unsigned
// update for a warm-tier signer must not evict a hot node, must not re-admit
// the signer, and must not consume its warm record.
static void test_updateuser_warm_signer_refusal_does_not_evict(void)
{
    const NodeNum signerNum = 0xE2000000 + 3;
    const int extra = MAX_NUM_NODES + 30; // overflow so the oldest non-protected demote to warm
    for (int i = 1; i <= extra; i++)
        db->push(0xE2000000 + i, /*lastHeard=*/i, /*keySeed=*/0x42);
    nodeInfoLiteSetBit(db->getMeshNode(signerNum), NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, true);

    db->runDemote();

    TEST_ASSERT_NULL(db->getMeshNode(signerNum)); // demoted out of hot
    TEST_ASSERT_TRUE(db->isKnownXeddsaSigner(signerNum));
    TEST_ASSERT_TRUE(db->isFull());
    const int hotBefore = (int)db->getNumMeshNodes();

    meshtastic_User u = makeUser("New name", "NN", /*keySeed=*/0x42);
    TEST_ASSERT_FALSE(db->updateUser(signerNum, u, 0, /*xeddsaSigned=*/false));

    TEST_ASSERT_EQUAL_INT(hotBefore, (int)db->getNumMeshNodes());
    TEST_ASSERT_NULL(db->getMeshNode(signerNum));         // not re-admitted
    TEST_ASSERT_TRUE(db->isKnownXeddsaSigner(signerNum)); // warm record intact (take() never ran)

    // Signed control: the same update signed is accepted and re-admits the
    // signer from warm with its key and signer bit restored.
    TEST_ASSERT_TRUE(db->updateUser(signerNum, u, 0, /*xeddsaSigned=*/true));
    const meshtastic_NodeInfoLite *back = db->getMeshNode(signerNum);
    TEST_ASSERT_NOT_NULL(back);
    TEST_ASSERT_TRUE(nodeInfoLiteHasXeddsaSigned(back));
    TEST_ASSERT_EQUAL_STRING("New name", back->long_name);
    assertStoredKeyEquals(signerNum, 0x42);
}
#endif // WARM_NODE_COUNT > 0

#endif // !(MESHTASTIC_EXCLUDE_PKI)

// --- persistence ---

// The erasure guard's outcome must survive the disk round trip: after a keyless
// add_contact against a pinned key, a rebooted NodeDB still holds the full key
// (pre-#11432 the zeroed key was persisted, breaking DMs until re-exchange).
static void test_contact_key_guard_survives_reboot(void)
{
    // saveNodeDatabaseToDisk() skips keyless devices, so give ourselves a key.
    fillKey(owner.public_key, 0x5A);

    meshtastic_SharedContact keyed = makeContact(kPeer, "Alice", "AL", /*keySeed=*/0x42);
    keyed.manually_verified = true;
    db->addFromContact(keyed); // persists
    // The keyless pre-DM contact for a verified node also carries manually_verified
    // (a non-verified keyless contact would be refused by the verified pin instead).
    meshtastic_SharedContact keyless = makeContact(kPeer, "Al2", "A2");
    keyless.manually_verified = true;
    db->addFromContact(keyless); // keyless merge; persists the guard result
    assertStoredKeyEquals(kPeer, 0x42);

    // A real cold boot starts with a zeroed nodeDatabase global; in-process the decode
    // callback appends on top of the previous boot's rows, so without this reset the
    // lookups below would find the pre-reboot RAM row and the persistence claim is vacuous.
    delete db;
    db = nullptr;
    nodeDB = nullptr;
    nodeDatabase.version = 0;
    nodeDatabase.nodes.clear();
    nodeDatabase.positions.clear();
    nodeDatabase.telemetry.clear();
    nodeDatabase.environment.clear();
    nodeDatabase.status.clear();
    db = new NodeDBTestShim();
    nodeDB = db;

    const meshtastic_NodeInfoLite *info = db->getMeshNode(kPeer);
    TEST_ASSERT_NOT_NULL_MESSAGE(info, "contact must survive the reload");
    assertStoredKeyEquals(kPeer, 0x42);
    TEST_ASSERT_EQUAL_STRING("Al2", info->long_name);
    TEST_ASSERT_TRUE(nodeInfoLiteIsKeyManuallyVerified(info)); // pin survives the reboot too
}

// --- Unity lifecycle ---

void setUp(void)
{
    savedOwner = owner;
    savedConfig = config;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    owner.public_key.size = 0;

    mockService = new MockMeshService();
    service = mockService;

    db->clearHot();
    db->seedSelf();
    db->resetDuplicateWarned();
}

void tearDown(void)
{
    owner = savedOwner;
    config = savedConfig;
    service = nullptr;
    delete mockService;
    mockService = nullptr;
}

IH_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
#ifdef FSCom
    // NodeDB and MessageStore bracket their FS writes with spiLock; nothing in the
    // test environment creates it, so do it here (initSPI asserts it only runs once).
    if (!spiLock)
        initSPI();
#endif
    db = new NodeDBTestShim();
    nodeDB = db;

    UNITY_BEGIN();

    printf("\n=== addFromContact guards ===\n");
    RUN_TEST(test_contact_keyless_preserves_stored_key);
    RUN_TEST(test_contact_new_key_updates_stored_key);
    RUN_TEST(test_contact_verified_pin_blocks_mismatched_key);
    RUN_TEST(test_contact_verified_pin_blocks_keyless_unverified);
    RUN_TEST(test_contact_verified_pin_allows_matching_key);
    RUN_TEST(test_contact_manually_verified_bit_survives_updates);
    RUN_TEST(test_contact_should_ignore_blocks_but_keeps_key);
    RUN_TEST(test_contact_client_base_stamps_heard_not_favorite);
    RUN_TEST(test_contact_without_user_is_noop);

#if !(MESHTASTIC_EXCLUDE_PKI)
    printf("\n=== updateUser key pinning ===\n");
    RUN_TEST(test_updateuser_pinned_key_blocks_mismatch);
    RUN_TEST(test_updateuser_keyless_nodeinfo_dropped_wholesale);
    RUN_TEST(test_updateuser_first_key_accepted);
    RUN_TEST(test_updateuser_own_key_advert_notifies_once);
    RUN_TEST(test_updateuser_id_derived_from_nodenum);
    RUN_TEST(test_updateuser_unsigned_update_refused_for_hot_signer);
    RUN_TEST(test_updateuser_signed_update_cannot_rotate_pinned_key);
#if WARM_NODE_COUNT > 0
    RUN_TEST(test_updateuser_warm_signer_refusal_does_not_evict);
#endif
#endif

    printf("\n=== persistence ===\n");
    RUN_TEST(test_contact_key_guard_survives_reboot);

    exit(UNITY_END());
}
IH_TEST_ENTRY void loop() {}
