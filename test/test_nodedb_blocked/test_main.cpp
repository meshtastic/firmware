// Tests for the NodeDB hot-store migration and favourite/ignored (blocked)
// retention paths - src/mesh/NodeDB.cpp.
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

#include "FSCommon.h" // FSCom, for the persistence round-trip tests
#include "mesh/NodeDB.h"
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
    // The persistence pair the NodeDB constructor runs. Private in NodeDB; reachable
    // here through the `friend class NodeDBTestShim` declaration.
    void runLoad() { loadFromDisk(); }
    bool runSave() { return saveNodeDatabaseToDisk(); }
    void stampUntrusted(NodeNum num, uint32_t uptimeSecs) { recordHeardWhileClockUntrusted(num, uptimeSecs); }

    // Read back the role + protected category the warm tier cached for a node.
    bool warmMeta(NodeNum n, uint8_t &role, uint8_t &prot) { return warmStore.lookupMeta(n, role, prot); }
    bool warmTake(NodeNum n, WarmNodeEntry &out) { return warmStore.take(n, out); }

    void clearHot()
    {
        meshNodes->clear();
        numMeshNodes = 0;
    }

    // The warm tier outlives setUp() (and a prior run's warm.dat), so a test that
    // asserts on a warm row has to start from an empty one.
    void clearWarm() { warmStore.clear(); }

    // keySize < 32 seeds a partial key, as a truncated/short NodeInfo would leave behind.
    void push(NodeNum num, uint32_t lastHeard, bool favorite, bool ignored, bool withUser, bool withKey,
              meshtastic_Config_DeviceConfig_Role role = meshtastic_Config_DeviceConfig_Role_CLIENT, pb_size_t keySize = 32)
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
            n.public_key.size = keySize;
            memset(n.public_key.bytes, static_cast<uint8_t>(num & 0xff), keySize);
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

// saveNodeDatabaseToDisk() returns early - "Skip NodeDB without key" - until this node
// has a PKI keypair, so a test that means to write nodes.proto has to supply one. It has
// to be re-armed before *every* save, because `owner` is a reference into devicestate,
// which loadFromDisk() reloads from disk underneath it.
void armSaveGate()
{
    owner.public_key.size = 32;
    memset(owner.public_key.bytes, 0x5A, sizeof(owner.public_key.bytes));
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

// A warm record stores 32 raw key bytes with no length, so a partial hot-store key would be
// indistinguishable from a real one once demoted. It must land as a keyless placeholder instead.
static void test_migration_dropsShortKeyOnDemotion(void)
{
    db->clearWarm();
    db->seedSelf();
    const NodeNum shortKeyNum = 2000 + 3;
    const NodeNum fullKeyNum = 2000 + 4;
    const int extra = MAX_NUM_NODES + 30; // overflow so the oldest non-protected are demoted
    // Warm entries steal the low 7 bits of last_heard for role and protected-category metadata
    // (WARM_TIME_MASK), so seed multiples of 128 to keep the values representable once demoted.
    for (int i = 1; i <= extra; i++)
        db->push(2000 + i, /*last_heard=*/(uint32_t)i * 128, /*favorite=*/false, /*ignored=*/false, /*withUser=*/true,
                 /*withKey=*/true, meshtastic_Config_DeviceConfig_Role_CLIENT,
                 /*keySize=*/(NodeNum)(2000 + i) == shortKeyNum ? 31 : 32);

    db->runDemote();

    // Both left the hot store; only the full key is allowed through to the warm tier.
    TEST_ASSERT_NULL(db->getMeshNode(shortKeyNum));
    TEST_ASSERT_NULL(db->getMeshNode(fullKeyNum));
    TEST_ASSERT_FALSE_MESSAGE(warmHasKey(shortKeyNum), "a 31-byte key must not be demoted as if it were a full key");
    TEST_ASSERT_TRUE_MESSAGE(warmHasKey(fullKeyNum), "a full 32-byte key still survives demotion");

    // The short-key node is still held, just keyless, so re-admission restores its last_heard.
    uint8_t role = 0xFF, prot = 0xFF;
    TEST_ASSERT_TRUE_MESSAGE(db->warmMeta(shortKeyNum, role, prot), "keyless placeholder row must still be present");
    WarmNodeEntry placeholder = {};
    TEST_ASSERT_TRUE_MESSAGE(db->warmTake(shortKeyNum, placeholder), "placeholder must be readable from the warm tier");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3u * 128, warmTimeOf(placeholder), "the keyless placeholder must carry last_heard");
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
static void test_removeNodeByNum_absentNodeFullDb(void)
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
static void test_removeNodeByNum_presentNodeFullDb(void)
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

// A save/load cycle must *replace* the in-RAM store, not add to it.
//
// loadProto() memsets every proto's destination clear before decoding except the two
// NodeDatabase descriptors, which hold std::vector members that memset would corrupt.
// Nothing took over that job, the nodes decode callback only push_back()s, and
// `nodeDatabase` is a file-scope global that outlives any one NodeDB - so every load
// appended the file's rows to whatever was already in RAM, and each cycle doubled the
// store: 1 -> 2 -> 4 -> ... -> MAX_NUM_NODES copies of one node number. Duplicates that
// are ignored (blocked) survive cleanupMeshDB() by design, which is what let a fixture
// reach a full DB of protected rows and take the NULL branch tested further down.
//
// Two nearby paths got this right and are the reason the gap was easy to miss:
// armNodeDatabaseDecodeTargets(), called immediately before the load, clears the
// satellite maps, and migrateLegacyNodeDatabase() clears the vector before it fills it.
// Only the current-version path did not.
//
// It is not test-only. reloadFromDisk() calls loadFromDisk() a second time in one process
// after an encrypted-storage unlock, and the locked boot returns early from it having run
// installDefaultNodeDatabase() - so the reload decoded the real store on top of
// MAX_NUM_NODES zeroed rows, went over cap, and nodeDBSelfCare() truncated and rewrote
// nodes.proto with cleanupMeshDB() never running on that path.
static void test_saveThenLoad_doesNotAccumulate(void)
{
    db->clearHot();
    db->seedSelf();
    db->push(0x0A000001, /*last_heard=*/100, false, false, /*withUser=*/true, /*withKey=*/false);
    db->push(0x0A000002, /*last_heard=*/200, false, false, /*withUser=*/true, /*withKey=*/false);
    const int expected = (int)db->getNumMeshNodes();
    TEST_ASSERT_EQUAL_INT(3, expected); // self + two

    // Three cycles, because one cycle of the old defect only produced a plausible-looking
    // 6: the signature is that it keeps doubling.
    for (int cycle = 0; cycle < 3; cycle++) {
        armSaveGate();
        TEST_ASSERT_TRUE(db->runSave());
        db->runLoad();
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected, (int)db->getNumMeshNodes(),
                                      "a save/load cycle must round-trip the store, not append to it");
    }
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0A000001));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0A000002));
}

// The other half of the same defect: with nothing resetting the destination, an absent or
// undecodable nodes.proto left the previous contents in RAM *and* kept the version that
// was decoded last time, so the install-defaults path was skipped and a "fresh" NodeDB
// silently inherited the old store.
static void test_loadFromDisk_absentFileDoesNotInheritStaleNodes(void)
{
    db->clearHot();
    db->seedSelf();
    db->push(0x0B000001, /*last_heard=*/100, false, false, /*withUser=*/true, /*withKey=*/false);
    armSaveGate();
    TEST_ASSERT_TRUE(db->runSave());

    FSCom.remove(nodeDatabaseFileName);
    db->runLoad();

    TEST_ASSERT_EQUAL_INT(0, (int)db->getNumMeshNodes());
    TEST_ASSERT_NULL(db->getMeshNode(0x0B000001));
    TEST_ASSERT_NULL(db->getMeshNode(0x0BADF00D)); // not even self survives an empty store

    // Leave the file state-manifest.tsv declares on disk. clearHot() first: the defaults the absent
    // file installed leave MAX_NUM_NODES zeroed rows in the vector the encode walks.
    armSaveGate();
    db->clearHot();
    TEST_ASSERT_TRUE(db->runSave());
}

// The NULL return in getOrCreateMeshNode(): a store at MAX_NUM_NODES whose every
// non-self row is protected has no eviction candidate, so admission must be refused
// rather than appending past the cap. setProtectedFlag() holds numProtectedNodes() to
// MAX_NUM_NODES-2 at runtime, so only a pre-cap or externally written nodes.proto can
// reach this shape - which is why the guard is not dead code, and why it is asserted
// here rather than assumed.
static void test_getOrCreateMeshNode_refusesWhenFullAndEveryCandidateProtected(void)
{
    db->clearHot();
    db->seedSelf();
    for (int i = 1; i < MAX_NUM_NODES; i++)
        db->push(0x0C000000 + i, /*last_heard=*/i, /*favorite=*/false, /*ignored=*/true, /*withUser=*/true, /*withKey=*/false);
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 1, db->numProtectedNodes()); // past the runtime cap, on purpose

    TEST_ASSERT_NULL(db->getOrCreateMeshNode(0x0CFFFFFF));

    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());         // nothing appended
    TEST_ASSERT_EQUAL_UINT32(MAX_NUM_NODES, (uint32_t)db->meshNodes->size()); // and no growth past the cap
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0C000001));                        // refusal evicted nobody
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0BADF00D));
}

// Complement, and the reason the cap is MAX_NUM_NODES-2: with the protected limit
// honoured, a full store still admits a new node - the guard above can only fire on a
// store that never went through setProtectedFlag().
static void test_getOrCreateMeshNode_admitsAtCapWhenProtectedLimitHonoured(void)
{
    db->clearHot();
    db->seedSelf();
    for (int i = 1; i <= MAX_NUM_NODES - 2; i++)
        db->push(0x0D000000 + i, /*last_heard=*/1000 + i, /*favorite=*/true, false, /*withUser=*/true, /*withKey=*/false);
    db->push(0x0DFFFF01, /*last_heard=*/1, false, false, /*withUser=*/true, /*withKey=*/false); // the evictable row
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes());
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES - 2, db->numProtectedNodes());

    meshtastic_NodeInfoLite *fresh = db->getOrCreateMeshNode(0x0DFFFF02);

    TEST_ASSERT_NOT_NULL(fresh);
    TEST_ASSERT_EQUAL_UINT32(0x0DFFFF02, fresh->num);
    TEST_ASSERT_NULL(db->getMeshNode(0x0DFFFF01));                    // the one evictable row was the victim
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x0D000001));                // no favourite was touched
    TEST_ASSERT_EQUAL_INT(MAX_NUM_NODES, (int)db->getNumMeshNodes()); // still exactly at the cap
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
    RUN_TEST(test_migration_dropsShortKeyOnDemotion);
    RUN_TEST(test_eviction_preservesFavorite);
    RUN_TEST(test_eviction_prefersCurrentBootStampOverPost2038Epoch);
    RUN_TEST(test_ignored_survivesEvictionAndCleanup);
    RUN_TEST(test_protectedCap_refusesBeyondLimit);
    RUN_TEST(test_removeNodeByNum_absentNodeFullDb);
    RUN_TEST(test_removeNodeByNum_presentNodeFullDb);
    RUN_TEST(test_saveThenLoad_doesNotAccumulate);
    RUN_TEST(test_loadFromDisk_absentFileDoesNotInheritStaleNodes);
    RUN_TEST(test_getOrCreateMeshNode_refusesWhenFullAndEveryCandidateProtected);
    RUN_TEST(test_getOrCreateMeshNode_admitsAtCapWhenProtectedLimitHonoured);
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
