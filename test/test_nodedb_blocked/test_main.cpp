// Tests for the NodeDB hot-store migration, favourite/ignored (blocked)
// retention, and addFromContact() public-key retention paths - src/mesh/NodeDB.cpp.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides WARM_NODE_COUNT / MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDB_TEST_ENTRY extern "C"
#else
#define NDB_TEST_ENTRY
#endif

// The migration demotes overflow into the warm tier, so these tests need it.
#if WARM_NODE_COUNT > 0

#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/admin.pb.h" // meshtastic_SharedContact
#include <cstring>

// Subclass shim: exposes the private maintenance paths (via the friend
// declaration in NodeDB.h) and lets a test own the hot store directly
// (meshNodes/numMeshNodes are public). Declared at global scope so it matches
// `friend class NodeDBTestShim` - an anonymous-namespace class would not.
class NodeDBTestShim : public NodeDB
{
  public:
    void runDemote() { demoteOldestHotNodesToWarm(); }
    void runCleanup() { cleanupMeshDB(); }

    // Read back the role + protected category the warm tier cached for a node.
    bool warmMeta(NodeNum n, uint8_t &role, uint8_t &prot) { return warmStore.lookupMeta(n, role, prot); }

    void clearHot()
    {
        meshNodes->clear();
        numMeshNodes = 0;
    }

    void push(NodeNum num, uint32_t lastHeard, bool favorite, bool ignored, bool withUser, bool withKey,
              meshtastic_Config_DeviceConfig_Role role = meshtastic_Config_DeviceConfig_Role_CLIENT)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = lastHeard;
        n.role = role;
        if (favorite)
            nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_IS_FAVORITE_MASK, true);
        if (ignored)
            nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_IS_IGNORED_MASK, true);
        if (withUser)
            nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
        if (withKey) {
            n.public_key.size = 32;
            memset(n.public_key.bytes, static_cast<uint8_t>(num & 0xff), 32);
            n.public_key.bytes[0] = 0x01; // ensure non-zero (all-zero == "no key")
        }
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();
    }

    // Index 0 is our own node; the eviction/migration scans treat it as self.
    void seedSelf() { push(0x0BADF00D, 0xFFFFFFFFu, false, false, /*withUser=*/true, /*withKey=*/false); }
};

namespace
{

NodeDBTestShim *db = nullptr;

bool warmHasKey(NodeNum n)
{
    meshtastic_NodeInfoLite_public_key_t k = {0, {0}};
    return db->copyPublicKey(n, k) && k.size == 32;
}

// Seed a hot-store node holding a full 32-byte key of repeated `fill`, copied out to `out`.
void seedKeyedNode(NodeNum num, uint8_t fill, uint8_t out[32])
{
    db->push(num, /*last_heard=*/100, /*favorite=*/false, /*ignored=*/false, /*withUser=*/true, /*withKey=*/false);
    meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    n->public_key.size = 32;
    memset(n->public_key.bytes, fill, 32);
    memcpy(out, n->public_key.bytes, 32);
}

// An add_contact payload for `num` whose key is `size` bytes of repeated `fill`.
meshtastic_SharedContact makeContact(NodeNum num, pb_size_t size, uint8_t fill)
{
    meshtastic_SharedContact c = meshtastic_SharedContact_init_zero;
    c.node_num = num;
    c.has_user = true;
    strncpy(c.user.long_name, "Contact", sizeof(c.user.long_name) - 1);
    strncpy(c.user.short_name, "ct", sizeof(c.user.short_name) - 1);
    c.user.public_key.size = size;
    memset(c.user.public_key.bytes, fill, size);
    return c;
}

} // namespace

void setUp(void)
{
    db->clearHot();
}
void tearDown(void) {}

// Migration: a database from a larger-cap build trims to MAX_NUM_NODES; the
// oldest non-protected nodes are demoted into the warm tier (keys preserved),
// while self, favourites and ignored survive even when they are the oldest.
static void test_migration_demotesOldestKeepsKeepersAndSelf(void)
{
    db->seedSelf();
    const int extra = MAX_NUM_NODES + 30; // overflow well past the MAX-2 cap
    for (int i = 1; i <= extra; i++) {
        const bool fav = (i == 1); // oldest, but a favourite
        const bool ign = (i == 2); // 2nd-oldest, but blocked
        db->push(2000 + i, /*last_heard=*/i, fav, ign, /*withUser=*/true, /*withKey=*/true);
    }

    db->runDemote();

    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0BADF00D));   // self retained
    TEST_ASSERT_NOT_NULL(db->getMeshNode(2000 + 1));     // oldest favourite retained
    TEST_ASSERT_NOT_NULL(db->getMeshNode(2000 + 2));     // oldest ignored retained
    TEST_ASSERT_NOT_NULL(db->getMeshNode(2000 + extra)); // freshest retained
    TEST_ASSERT_NULL(db->getMeshNode(2000 + 3));         // oldest non-protected demoted out of hot
    TEST_ASSERT_TRUE(warmHasKey(2000 + 3));              // ...but its key kept in the warm tier
}

// Eviction carries the device role + protected category into the warm tier. A TRACKER is
// hop-protected but NOT eviction-protected, so it gets demoted with its key; the warm
// record must report role=TRACKER / category=Role. A plain CLIENT carries role=CLIENT/None.
static void test_migration_carriesRoleAndProtectedIntoWarm(void)
{
    db->seedSelf();
    const int extra = MAX_NUM_NODES + 30; // overflow so the oldest non-protected are demoted
    for (int i = 1; i <= extra; i++) {
        const auto role = (i == 3) ? meshtastic_Config_DeviceConfig_Role_TRACKER : meshtastic_Config_DeviceConfig_Role_CLIENT;
        db->push(2000 + i, /*last_heard=*/i, /*favorite=*/false, /*ignored=*/false, /*withUser=*/true,
                 /*withKey=*/true, role);
    }

    db->runDemote();

    uint8_t role = 0xFF, prot = 0xFF;
    // TRACKER (i=3): demoted out of hot, key kept, role + protected carried into warm.
    TEST_ASSERT_NULL(db->getMeshNode(2000 + 3));
    TEST_ASSERT_TRUE(warmHasKey(2000 + 3));
    TEST_ASSERT_TRUE(db->warmMeta(2000 + 3, role, prot));
    TEST_ASSERT_EQUAL(meshtastic_Config_DeviceConfig_Role_TRACKER, role);
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::Role, prot);
    // CLIENT (i=4): also demoted, carries role=CLIENT / category=None.
    TEST_ASSERT_TRUE(db->warmMeta(2000 + 4, role, prot));
    TEST_ASSERT_EQUAL(meshtastic_Config_DeviceConfig_Role_CLIENT, role);
    TEST_ASSERT_EQUAL((uint8_t)WarmProtected::None, prot);
}

// The signer bit is learned from verified traffic, not NodeInfo, so it must survive a warm
// round trip. The plain node is the control: re-admission restores it, it doesn't invent it.
static void test_migration_carriesSignerBitThroughWarm(void)
{
    db->seedSelf();
    const NodeNum signerNum = 2000 + 3;
    const NodeNum plainNum = 2000 + 4;
    const int extra = MAX_NUM_NODES + 30; // overflow so the oldest non-protected are demoted
    for (int i = 1; i <= extra; i++)
        db->push(2000 + i, /*last_heard=*/i, /*favorite=*/false, /*ignored=*/false, /*withUser=*/true, /*withKey=*/true);
    nodeInfoLiteSetBit(db->getMeshNode(signerNum), NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, true);
    TEST_ASSERT_TRUE(nodeInfoLiteHasXeddsaSigned(db->getMeshNode(signerNum)));

    db->runDemote();

    // Both are out of the hot store and held in the warm tier.
    TEST_ASSERT_NULL(db->getMeshNode(signerNum));
    TEST_ASSERT_NULL(db->getMeshNode(plainNum));

    const meshtastic_NodeInfoLite *back = db->getOrCreateMeshNode(signerNum);
    TEST_ASSERT_NOT_NULL(back);
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasXeddsaSigned(back), "signer bit must survive a warm-tier round trip");

    const meshtastic_NodeInfoLite *plainBack = db->getOrCreateMeshNode(plainNum);
    TEST_ASSERT_NOT_NULL(plainBack);
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasXeddsaSigned(plainBack), "re-admission must not invent the signer bit");
}

// Favourite handling: a favourite is never the eviction victim, even when it is
// the oldest node in a full hot store.
static void test_eviction_preservesFavorite(void)
{
    db->seedSelf();
    for (int i = 1; i < MAX_NUM_NODES; i++) { // fill to MAX_NUM_NODES total (incl. self)
        const bool fav = (i == 1);            // oldest non-self, favourite
        db->push(3000 + i, /*last_heard=*/i, fav, false, /*withUser=*/true, /*withKey=*/true);
    }
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes()); // full

    TEST_ASSERT_NOT_NULL(db->getOrCreateMeshNode(0x99990000)); // forces an eviction

    TEST_ASSERT_NOT_NULL(db->getMeshNode(3000 + 1)); // favourite survived despite being oldest
    TEST_ASSERT_NULL(db->getMeshNode(3000 + 2));     // oldest non-favourite evicted
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x99990000));
}

// Ignored handling: an ignored node survives eviction (like a favourite), and is
// never purged by cleanupMeshDB even with no user info (a block set by bare ID).
static void test_ignored_survivesEvictionAndCleanup(void)
{
    // (a) eviction protection
    db->clearHot();
    db->seedSelf();
    for (int i = 1; i < MAX_NUM_NODES; i++) {
        const bool ign = (i == 1); // oldest non-self, blocked
        db->push(4000 + i, /*last_heard=*/i, false, ign, /*withUser=*/true, /*withKey=*/true);
    }
    TEST_ASSERT_NOT_NULL(db->getOrCreateMeshNode(0x88880000));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(4000 + 1)); // blocked node survived
    TEST_ASSERT_NULL(db->getMeshNode(4000 + 2));     // oldest non-blocked evicted

    // (b) cleanup protection - ignored kept without user info, plain no-user purged
    db->clearHot();
    db->seedSelf();
    db->push(5000, 100, false, /*ignored=*/true, /*withUser=*/false, false);
    db->push(5001, 100, false, false, /*withUser=*/false, false);
    db->runCleanup();
    TEST_ASSERT_NOT_NULL(db->getMeshNode(5000)); // blocked-by-ID kept despite no user info
    TEST_ASSERT_NULL(db->getMeshNode(5001));     // ordinary no-user node purged
}

// Protected-node cap: at most MAX_NUM_NODES-2 nodes may be protected, so >=2
// evictable slots always remain. setProtectedFlag refuses once the cap is hit.
static void test_protectedCap_refusesBeyondLimit(void)
{
    db->seedSelf();
    for (int i = 0; i < MAX_NUM_NODES - 2; i++)
        db->push(6000 + i, 100, /*favorite=*/true, false, /*withUser=*/true, false);
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 2, db->numProtectedNodes());

    db->push(7000, 100, false, false, /*withUser=*/true, false);
    meshtastic_NodeInfoLite *fresh = db->getMeshNode(7000);
    TEST_ASSERT_NOT_NULL(fresh);
    TEST_ASSERT_FALSE(db->setProtectedFlag(fresh, NODEINFO_BITFIELD_IS_IGNORED_MASK, true)); // refused at cap
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(fresh));                                         // unchanged
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 2, db->numProtectedNodes());

    // Adding another flag to an already-protected node doesn't grow the set, so
    // it's still allowed at the cap.
    meshtastic_NodeInfoLite *already = db->getMeshNode(6000);
    TEST_ASSERT_TRUE(db->setProtectedFlag(already, NODEINFO_BITFIELD_IS_IGNORED_MASK, true));
}

// removeNodeByNum() compacts survivors down and clears the slots that leaves free. A full
// store with no matching node frees none, so there is nothing past the last node to clear.
static void test_removeNodeByNum_absentNodeOnFullDb(void)
{
    db->seedSelf();
    for (int i = 1; i < MAX_NUM_NODES; i++) // fill to MAX_NUM_NODES total (incl. self)
        db->push(8000 + i, /*last_heard=*/i, false, false, /*withUser=*/true, /*withKey=*/true);
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());

    db->removeNodeByNum(0xDEADBEEF); // absent; ASan flags a write past the last slot

    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes()); // nothing removed
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0BADF00D));                // self intact
    TEST_ASSERT_NOT_NULL(db->getMeshNode(8000 + 1));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(8000 + MAX_NUM_NODES - 1)); // last slot intact
}

// Control for the above: a matching node on a full store is still removed, the survivors
// compact down, and the freed tail slot is cleared.
static void test_removeNodeByNum_presentNodeOnFullDb(void)
{
    db->seedSelf();
    for (int i = 1; i < MAX_NUM_NODES; i++)
        db->push(8000 + i, /*last_heard=*/i, false, false, /*withUser=*/true, /*withKey=*/true);

    db->removeNodeByNum(8000 + 5);

    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 1, (int)db->getNumMeshNodes());
    TEST_ASSERT_NULL(db->getMeshNode(8000 + 5));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(8000 + 4));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(8000 + MAX_NUM_NODES - 1)); // survivors kept
}

// addFromContact() runs CopyUserToNodeInfoLite, which assigns public_key unconditionally.
// A keyless contact (clients send add_contact before every DM) must not erase a stored key.
static void test_addFromContact_emptyKeyKeepsStoredKey(void)
{
    db->seedSelf();
    const NodeNum num = 0xC0FFEE01;
    uint8_t stored[32];
    seedKeyedNode(num, 0xA5, stored);

    db->addFromContact(makeContact(num, /*size=*/0, /*fill=*/0x00));

    const meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT(32, n->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(stored, n->public_key.bytes, 32);
}

// Same guard for a truncated key: anything short of 32 bytes is unusable for PKI,
// so it must not displace a full stored key either.
static void test_addFromContact_shortKeyKeepsStoredKey(void)
{
    db->seedSelf();
    const NodeNum num = 0xC0FFEE02;
    uint8_t stored[32];
    seedKeyedNode(num, 0x5A, stored);

    db->addFromContact(makeContact(num, /*size=*/4, /*fill=*/0x11));

    const meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT(32, n->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(stored, n->public_key.bytes, 32);
}

// Regression guard on the guard: a well-formed 32-byte contact key still replaces the
// stored one. Refusing erasure must not harden into a blanket first-key-wins pin.
static void test_addFromContact_fullKeyReplacesStoredKey(void)
{
    db->seedSelf();
    const NodeNum num = 0xC0FFEE03;
    uint8_t stored[32];
    seedKeyedNode(num, 0x11, stored);
    uint8_t incoming[32];
    memset(incoming, 0x22, sizeof(incoming));

    db->addFromContact(makeContact(num, /*size=*/32, /*fill=*/0x22));

    const meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT(32, n->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(incoming, n->public_key.bytes, 32);
}

// The ordinary add_contact case: a full key is stored on an entry that held none.
static void test_addFromContact_fullKeyStoredWhenNoneHeld(void)
{
    db->seedSelf();
    const NodeNum num = 0xC0FFEE04;
    uint8_t incoming[32];
    memset(incoming, 0x33, sizeof(incoming));

    db->addFromContact(makeContact(num, /*size=*/32, /*fill=*/0x33));

    const meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT(32, n->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(incoming, n->public_key.bytes, 32);
}

NDB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    db = new NodeDBTestShim();
    nodeDB = db;

    UNITY_BEGIN();
    RUN_TEST(test_migration_demotesOldestKeepsKeepersAndSelf);
    RUN_TEST(test_migration_carriesRoleAndProtectedIntoWarm);
    RUN_TEST(test_migration_carriesSignerBitThroughWarm);
    RUN_TEST(test_eviction_preservesFavorite);
    RUN_TEST(test_ignored_survivesEvictionAndCleanup);
    RUN_TEST(test_protectedCap_refusesBeyondLimit);
    RUN_TEST(test_removeNodeByNum_absentNodeOnFullDb);
    RUN_TEST(test_removeNodeByNum_presentNodeOnFullDb);
    RUN_TEST(test_addFromContact_emptyKeyKeepsStoredKey);
    RUN_TEST(test_addFromContact_shortKeyKeepsStoredKey);
    RUN_TEST(test_addFromContact_fullKeyReplacesStoredKey);
    RUN_TEST(test_addFromContact_fullKeyStoredWhenNoneHeld);
    exit(UNITY_END());
}
NDB_TEST_ENTRY void loop() {}

#else // WARM_NODE_COUNT == 0 - nothing to exercise here

void setUp(void) {}
void tearDown(void) {}
NDB_TEST_ENTRY void setup()
{
    UNITY_BEGIN();
    exit(UNITY_END());
}
NDB_TEST_ENTRY void loop() {}

#endif
