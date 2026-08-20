// Tests for the NodeDB hot-store migration, favourite/ignored (blocked)
// retention and owner-recovery paths - src/mesh/NodeDB.cpp.
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
#include <ErriezCRC32.h>
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
    bool runOwnerRecovery() { return recoverOwnerFromNodeDB(); }
    void stampUntrusted(NodeNum num, uint32_t uptimeSecs) { recordHeardWhileClockUntrusted(num, uptimeSecs); }

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

// Owner-recovery fixtures. The provisional number is the macaddr-derived one
// pickNewNodeNum() leaves behind; our real row is stored under crc32(public_key).
const NodeNum provisionalNum = 0x1A2B3C4D;

NodeNum setSecurityKey(uint8_t fill)
{
    config.has_security = true;
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, fill, 32);
    return crc32Buffer(config.security.public_key.bytes, config.security.public_key.size);
}

void pushOwnerRow(NodeNum num, const char *longName, const char *shortName, bool licensed, bool unmessagable)
{
    db->push(num, /*last_heard=*/100, /*favorite=*/false, /*ignored=*/false, /*withUser=*/true, /*withKey=*/false);
    meshtastic_NodeInfoLite *n = db->getMeshNode(num);
    strncpy(n->long_name, longName, sizeof(n->long_name) - 1);
    strncpy(n->short_name, shortName, sizeof(n->short_name) - 1);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_LICENSED_MASK, licensed);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK, true);
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK, unmessagable);
}

// The state a discarded devicestate leaves behind: factory owner, provisional
// number, and no security config yet because config has not been read.
void installFactoryOwner()
{
    myNodeInfo.my_node_num = provisionalNum;
    memset(&owner, 0, sizeof(owner));
    strcpy(owner.long_name, "Meshtastic 3c4d");
    strcpy(owner.short_name, "3c4d");
    config.has_security = false;
    config.security.public_key.size = 0;
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

// A node heard during this boot is newer than every persisted epoch, including valid epochs after
// 2038. Ranking both domains in one uint32_t incorrectly evicts the current-boot node first.
static void test_eviction_prefersCurrentBootStampOverPost2038Epoch(void)
{
    constexpr NodeNum futureDated = 0x70000001;
    constexpr NodeNum heardThisBoot = 0x70000002;

    db->seedSelf();
    db->push(futureDated, 0xB5000000u, false, false, /*withUser=*/true, /*withKey=*/true);
    db->push(heardThisBoot, 0, false, false, /*withUser=*/true, /*withKey=*/true);
    db->stampUntrusted(heardThisBoot, 10);
    for (int i = 3; i < MAX_NUM_NODES; i++)
        db->push(0x70000000u + i, UINT32_MAX, false, false, /*withUser=*/true, /*withKey=*/true);

    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());
    TEST_ASSERT_NOT_NULL(db->getOrCreateMeshNode(0x79999999));

    TEST_ASSERT_NULL(db->getMeshNode(futureDated));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(heardThisBoot));
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

// Owner recovery after a discarded devicestate reads our own row, which lives under
// crc32(public_key) - not under the provisional number pickNewNodeNum() just handed us.
static void test_ownerRecovery_restoresFromKeyDerivedRow(void)
{
    installFactoryOwner();
    const NodeNum selfNum = setSecurityKey(0xA5);
    TEST_ASSERT_NOT_EQUAL(provisionalNum, selfNum);
    pushOwnerRow(selfNum, "Base Camp Repeater", "BCMP", /*licensed=*/true, /*unmessagable=*/true);

    TEST_ASSERT_TRUE(db->runOwnerRecovery());

    TEST_ASSERT_EQUAL_STRING("Base Camp Repeater", owner.long_name);
    TEST_ASSERT_EQUAL_STRING("BCMP", owner.short_name);
    TEST_ASSERT_TRUE(owner.is_licensed);
    TEST_ASSERT_TRUE(owner.has_is_unmessagable);
    TEST_ASSERT_TRUE(owner.is_unmessagable);
}

// A node DB that does not contain us is a clean miss: the factory owner stands, even
// though a foreign row happens to sit on the provisional number.
static void test_ownerRecovery_missKeepsFactoryOwner(void)
{
    installFactoryOwner();
    const NodeNum selfNum = setSecurityKey(0x5A);
    pushOwnerRow(provisionalNum, "Someone Elses Node", "ELSE", /*licensed=*/true, /*unmessagable=*/true);
    TEST_ASSERT_NULL(db->getMeshNode(selfNum)); // our own row really is absent

    TEST_ASSERT_FALSE(db->runOwnerRecovery());

    TEST_ASSERT_EQUAL_STRING("Meshtastic 3c4d", owner.long_name);
    TEST_ASSERT_EQUAL_STRING("3c4d", owner.short_name);
    TEST_ASSERT_FALSE(owner.is_licensed);
    TEST_ASSERT_FALSE(owner.has_is_unmessagable);
    TEST_ASSERT_FALSE(owner.is_unmessagable);
}

// Keyless / pre-PKI builds have no key to derive from, so recovery keeps probing the
// provisional number - which on those builds is our identity.
static void test_ownerRecovery_keylessUsesProvisionalNum(void)
{
    installFactoryOwner(); // leaves config.has_security false
    pushOwnerRow(provisionalNum, "Keyless Legacy Node", "KLGY", /*licensed=*/false, /*unmessagable=*/false);

    TEST_ASSERT_TRUE(db->runOwnerRecovery());

    TEST_ASSERT_EQUAL_STRING("Keyless Legacy Node", owner.long_name);
    TEST_ASSERT_EQUAL_STRING("KLGY", owner.short_name);
    TEST_ASSERT_FALSE(owner.is_licensed);
    TEST_ASSERT_TRUE(owner.has_is_unmessagable); // the row carries the flag explicitly
    TEST_ASSERT_FALSE(owner.is_unmessagable);
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
    RUN_TEST(test_eviction_prefersCurrentBootStampOverPost2038Epoch);
    RUN_TEST(test_ignored_survivesEvictionAndCleanup);
    RUN_TEST(test_protectedCap_refusesBeyondLimit);
    RUN_TEST(test_removeNodeByNum_absentNodeOnFullDb);
    RUN_TEST(test_removeNodeByNum_presentNodeOnFullDb);
    RUN_TEST(test_ownerRecovery_restoresFromKeyDerivedRow);
    RUN_TEST(test_ownerRecovery_missKeepsFactoryOwner);
    RUN_TEST(test_ownerRecovery_keylessUsesProvisionalNum);
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
