// NodeDB save-failure contract, NodeDB::saveProto() and NodeDB::saveToDisk() in src/mesh/NodeDB.cpp.
//
// saveProto() must report a write that did not land. SafeFile::close() verifies the .tmp by hash and
// renames it over the live file; saveProto() used to capture that result and then return the
// pb_encode status alone, so a torn page or a failed rename counted as a successful save and the
// caller's recovery never ran. The regression guarded: saveProto() returning true while the live
// file was never replaced.
//
// saveToDisk() must not answer a failed write with fsFormat(). A write that fails once is far more
// often a rail dip or a busy SoftDevice than a corrupt filesystem, and the format takes every file on
// the device with it; on nRF52 that is the "critical error 12/13" report followed by a new node
// identity. The contract pinned here: a transient failure is retried and the retry lands, and a rail
// that is still unsafe at the retry gate makes saveToDisk() return false and leave the filesystem
// alone. The regression guarded: any path from a single failed write straight into fsFormat().
//
// The rail is driven through powerHAL_isPowerLevelSafe(), whose native default is a weak "always
// safe"; this suite supplies a strong, scripted definition. Windows links the default strongly, so
// the gate-driven cases are compiled out there and only the rename case runs. The format branch
// itself cannot be reached in a native test: on portduino a FLASH_CORRUPTION critical error exits the
// process, which is exactly the outcome the assertions here prove is not taken.
//
// NodeDB::saveNodeDatabaseIfDirty() and the reboot observer must eventually persist a node change that
// updateUser() deferred under its once-a-minute budget. updateUser() used to skip the save outright
// when the last one was under a minute old, so a NodeInfo from a new peer heard in that window was
// never written and the peer was gone after the next restart (#11928). The contract pinned: a pending
// change survives the throttle and is written once the minute passes, and a reboot writes it at once.
// The regression guarded: a dirty node database that is never saved.
// NodeDB::updateFrom() in src/mesh/NodeDB.cpp must also schedule a save when a known peer's RF slot
// changes. Otherwise a restart can restore stale LoRa evidence even though the radio heard that peer.
#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NSR_TEST_ENTRY extern "C"
#else
#define NSR_TEST_ENTRY
#endif

#include "FSCommon.h" // defines FSCom; must precede the feature guard below

#if defined(FSCom)

#include "UptimeClock.h" // test clock
#include "mesh/NodeDB.h"
#include "power/PowerHAL.h"
#include "sleep.h" // notifyReboot
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

// Friend seam declared in NodeDB.h (PIO_UNIT_TESTING): drive the deferred-save state directly.
// Never instantiated - constructing one would run the real boot sequence.
class NodeDBTestShim : public NodeDB
{
  public:
    static void markDirty(NodeDB *db, uint32_t lastSaveMs)
    {
        db->nodeDatabaseDirty = true;
        db->lastNodeDbSave = lastSaveMs;
    }
    static bool dirty(const NodeDB *db) { return db->nodeDatabaseDirty; }
    // A node save "just happened", so updateUser()'s next save is deferred by the one-minute throttle.
    static void markSaved(NodeDB *db, uint32_t lastSaveMs) { db->lastNodeDbSave = lastSaveMs; }
};

namespace
{

// --- Scripted rail -------------------------------------------------------------------------------
// Each powerHAL_isPowerLevelSafe() call consumes one entry; past the end the rail reads safe.
// saveToDisk() asks at entry, saveToDiskNoRetry() and saveProto() ask again, and the retry gate asks
// before each retry, so the second reading is the first one a write can see.
std::vector<bool> railScript;
size_t railCalls = 0;

void scriptRail(std::initializer_list<bool> readings)
{
    railScript.assign(readings);
    railCalls = 0;
}

// --- File helpers ----------------------------------------------------------------------------------

bool readFileBytes(const char *path, std::vector<uint8_t> &out)
{
    out.clear();
    File f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return false;
    uint8_t buf[512];
    size_t n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
        out.insert(out.end(), buf, buf + n);
    f.close();
    return true;
}

void writeFileBytes(const char *path, const std::vector<uint8_t> &bytes)
{
    FSCom.remove(path); // FILE_O_WRITE is append on some backends; start clean
    File f = FSCom.open(path, FILE_O_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, path);
    TEST_ASSERT_EQUAL_size_t(bytes.size(), f.write(bytes.data(), bytes.size()));
    f.close();
}

// FNV-1a content fingerprint; answers only "did this file change?". 0 == missing file.
uint64_t fileFingerprint(const char *path)
{
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes))
        return 0;
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

// Make the next persisted config differ from the last one, so a landed write is visible.
void bumpConfig()
{
    config.device.node_info_broadcast_secs += 1;
}

std::optional<meshtastic_NodeInfoLite> persistedNode(NodeNum num)
{
    meshtastic_NodeDatabase reloaded{};
    if (nodeDB->loadProto(nodeDatabaseFileName, nodeDB->getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                          &meshtastic_NodeDatabase_msg, &reloaded) != LoadFileResult::LOAD_SUCCESS)
        return std::nullopt;
    for (const auto &n : reloaded.nodes) {
        if (n.num == num)
            return n;
    }
    return std::nullopt;
}

// A peer heard for the first time, with a key so updateUser() takes the normal path.
meshtastic_User makePeer(const char *longName)
{
    meshtastic_User u = meshtastic_User_init_zero;
    strncpy(u.long_name, longName, sizeof(u.long_name) - 1);
    strncpy(u.short_name, "PR", sizeof(u.short_name) - 1);
    u.public_key.size = 32;
    memset(u.public_key.bytes, 0x5A, 32);
    u.public_key.bytes[0] = 0x01;
    return u;
}

// PortduinoFS::rmdir() is unlink() underneath and cannot remove a directory, so go to the host.
bool removeHostDirectory(const char *fsPath)
{
    std::string host = std::string(getenv("HOME")) + "/.portduino/default" + fsPath;
    return ::rmdir(host.c_str()) == 0;
}

} // namespace

#if !defined(_WIN32)
// Strong definition shadows the weak native default in src/power/PowerHAL.cpp.
bool powerHAL_isPowerLevelSafe()
{
    if (railCalls < railScript.size())
        return railScript[railCalls++];
    railCalls++;
    return true;
}
#endif

// --- saveProto ------------------------------------------------------------------------------------

// A directory squatting on the live path makes the final rename fail after the .tmp was written and
// verified, which is the only failure SafeFile::close() reports that pb_encode cannot see.
static void test_saveProto_failedRename_returnsFalse(void)
{
    TEST_MESSAGE("=== saveProto: a failed rename is reported, not swallowed ===");
    std::vector<uint8_t> live;
    TEST_ASSERT_TRUE(readFileBytes(configFileName, live));
    TEST_ASSERT_TRUE(FSCom.remove(configFileName));
    TEST_ASSERT_TRUE(FSCom.mkdir(configFileName));

    const bool saved = nodeDB->saveProto(configFileName, meshtastic_LocalConfig_size, &meshtastic_LocalConfig_msg, &config);

    // Restore the sandbox before asserting so a failure does not leak the directory into later tests.
    std::string tmp = std::string(configFileName) + ".tmp";
    FSCom.remove(tmp.c_str());
    TEST_ASSERT_TRUE(removeHostDirectory(configFileName));
    writeFileBytes(configFileName, live);

    TEST_ASSERT_FALSE(saved);
}

#if !defined(_WIN32)
// --- saveToDisk retry gate -------------------------------------------------------------------------

static void test_saveToDisk_railDipDuringWrite_retriesAndLands(void)
{
    TEST_MESSAGE("=== saveToDisk: one unsafe reading mid-write is retried, no format ===");
    const uint64_t before = fileFingerprint(configFileName);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, before);
    bumpConfig();

    scriptRail({true, false}); // entry safe, the write itself sees the dip, the retry gate is safe

    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_CONFIG));

    TEST_ASSERT_GREATER_THAN_size_t(2, railCalls); // the retry gate was consulted
    TEST_ASSERT_NOT_EQUAL_UINT64(before, fileFingerprint(configFileName));
    TEST_ASSERT_TRUE(FSCom.exists(deviceStateFileName)); // an fsFormat() would have taken this too
}

static void test_saveToDisk_railStillUnsafeAtRetry_bailsWithoutFormat(void)
{
    TEST_MESSAGE("=== saveToDisk: rail unsafe at the retry gate returns false, no format ===");
    const uint64_t before = fileFingerprint(configFileName);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, before);
    bumpConfig();

    scriptRail({true, false, false, false, false}); // never recovers while this save is in progress

    // On portduino a FLASH_CORRUPTION critical error exits the process, so surviving this call is
    // itself the proof that no format was attempted.
    TEST_ASSERT_FALSE(nodeDB->saveToDisk(SEGMENT_CONFIG));

    TEST_ASSERT_EQUAL_UINT64(before, fileFingerprint(configFileName));
    TEST_ASSERT_TRUE(FSCom.exists(deviceStateFileName));
}

// The whole point of the retry gate: a write that keeps failing while the filesystem still reads is a
// busy or lock-protected flash, not corruption, and formatting would take every other file with it.
static void test_saveToDisk_writeFailsButFsReadable_doesNotFormat(void)
{
    TEST_MESSAGE("=== saveToDisk: unwritable but readable filesystem must not be formatted ===");
    std::vector<uint8_t> live;
    TEST_ASSERT_TRUE(readFileBytes(configFileName, live));
    const uint64_t deviceStateBefore = fileFingerprint(deviceStateFileName);

    // A directory on the live path fails every rename, so all retries fail - while /prefs and the
    // other protos stay perfectly readable.
    TEST_ASSERT_TRUE(FSCom.remove(configFileName));
    TEST_ASSERT_TRUE(FSCom.mkdir(configFileName));

    const bool saved = nodeDB->saveToDisk(SEGMENT_CONFIG);

    std::string tmp = std::string(configFileName) + ".tmp";
    FSCom.remove(tmp.c_str());
    TEST_ASSERT_TRUE(removeHostDirectory(configFileName));
    writeFileBytes(configFileName, live);

    TEST_ASSERT_FALSE(saved);
    // The decisive assertion: an fsFormat() would have taken devicestate with it.
    TEST_ASSERT_TRUE(FSCom.exists(deviceStateFileName));
    TEST_ASSERT_EQUAL_UINT64(deviceStateBefore, fileFingerprint(deviceStateFileName));
}

static void test_saveToDisk_railUnsafeAtEntry_returnsFalseImmediately(void)
{
    TEST_MESSAGE("=== saveToDisk: rail unsafe at entry writes nothing ===");
    const uint64_t before = fileFingerprint(configFileName);
    bumpConfig();

    scriptRail({false});

    TEST_ASSERT_FALSE(nodeDB->saveToDisk(SEGMENT_CONFIG));

    TEST_ASSERT_EQUAL_size_t(1, railCalls);
    TEST_ASSERT_EQUAL_UINT64(before, fileFingerprint(configFileName));
}
#endif // !_WIN32

// --- Deferred node database save -------------------------------------------------------------------

static void test_saveNodeDatabaseIfDirty_waitsOutThrottleThenSaves(void)
{
    TEST_MESSAGE("=== saveNodeDatabaseIfDirty: a deferred change is written once the minute passes ===");
    Time::setTestMillis(10 * 60 * 1000);
    NodeDBTestShim::markDirty(nodeDB, Time::getMillis()); // a save just happened, so this change was deferred

    nodeDB->saveNodeDatabaseIfDirty();
    TEST_ASSERT_TRUE(NodeDBTestShim::dirty(nodeDB)); // still inside the minute: nothing written yet

    Time::advanceTestMillis(60 * 1000);
    nodeDB->saveNodeDatabaseIfDirty();
    TEST_ASSERT_FALSE(NodeDBTestShim::dirty(nodeDB)); // cleared only by a save that landed
}

static void test_rebootObserver_savesDirtyNodeDatabase(void)
{
    TEST_MESSAGE("=== reboot: a pending node change is written before restart ===");
    Time::setTestMillis(20 * 60 * 1000);
    NodeDBTestShim::markDirty(nodeDB, Time::getMillis()); // the throttle alone would hold it back

    notifyReboot.notifyObservers(NULL);

    TEST_ASSERT_FALSE(NodeDBTestShim::dirty(nodeDB));
}

// The same contract driven from a real peer: a NodeInfo arriving inside the throttle window must reach
// nodes.proto once the window passes, not just clear the dirty flag.
static void test_updateUser_deferredPeer_isPersistedAfterThrottle(void)
{
    TEST_MESSAGE("=== updateUser: a peer deferred by the throttle is written to nodes.proto later ===");
    constexpr NodeNum kPeer = 0xE1000101;
    Time::setTestMillis(30 * 60 * 1000);
    NodeDBTestShim::markSaved(nodeDB, Time::getMillis());

    meshtastic_User u = makePeer("Bob");
    TEST_ASSERT_TRUE(nodeDB->updateUser(kPeer, u));

    TEST_ASSERT_FALSE(persistedNode(kPeer).has_value()); // deferred: not on flash yet

    Time::advanceTestMillis(60 * 1000);
    nodeDB->saveNodeDatabaseIfDirty();

    const auto saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_EQUAL_STRING("Bob", saved->long_name);
}

// A reboot inside the throttle window must not lose a peer heard just before it.
static void test_updateUser_deferredPeer_isPersistedOnReboot(void)
{
    TEST_MESSAGE("=== updateUser: a deferred peer is written to nodes.proto at reboot ===");
    constexpr NodeNum kPeer = 0xE1000102;
    Time::setTestMillis(40 * 60 * 1000);
    NodeDBTestShim::markSaved(nodeDB, Time::getMillis());

    meshtastic_User u = makePeer("Carol");
    TEST_ASSERT_TRUE(nodeDB->updateUser(kPeer, u));

    TEST_ASSERT_FALSE(persistedNode(kPeer).has_value());

    notifyReboot.notifyObservers(NULL);

    const auto saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_EQUAL_STRING("Carol", saved->long_name);
}

static void test_updateFrom_rfSlotChanges_arePersisted(void)
{
    constexpr NodeNum kPeer = 0xE1000103;
    constexpr NodeNum kAnonymous = 0xE1000104;
    Time::setTestMillis(50 * 60 * 1000);
    meshtastic_User user = makePeer("Drew");
    TEST_ASSERT_TRUE(nodeDB->updateUser(kPeer, user));
    NodeDBTestShim::markSaved(nodeDB, Time::getMillis());

    auto saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_FALSE(nodeInfoLiteHasRfHear(&*saved));

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    packet.from = kAnonymous;
    nodeDB->updateFrom(packet);
    TEST_ASSERT_FALSE(NodeDBTestShim::dirty(nodeDB));

    packet.from = kPeer;
    nodeDB->updateFrom(packet);
    const uint16_t homeSlot = nodeDB->committedLoraSlot();
    TEST_ASSERT_TRUE(nodeInfoLiteHeardOnSlot(nodeDB->getMeshNode(kPeer), homeSlot));
    nodeDB->saveNodeDatabaseIfDirty();
    saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_FALSE(nodeInfoLiteHasRfHear(&*saved));

    Time::advanceTestMillis(60 * 1000);
    nodeDB->saveNodeDatabaseIfDirty();
    saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_TRUE(nodeInfoLiteHeardOnSlot(&*saved, homeSlot));

    nodeDB->updateFrom(packet);
    TEST_ASSERT_FALSE(NodeDBTestShim::dirty(nodeDB));

    const float homeFrequency = config.lora.override_frequency;
    config.lora.override_frequency = homeFrequency == 915.0f ? 916.0f : 915.0f;
    nodeDB->refreshCommittedLoraSlot();
    const uint16_t otherSlot = nodeDB->committedLoraSlot();
    nodeDB->updateFrom(packet);
    config.lora.override_frequency = homeFrequency;
    nodeDB->refreshCommittedLoraSlot();
    TEST_ASSERT_NOT_EQUAL_UINT16(homeSlot, otherSlot);

    notifyReboot.notifyObservers(nullptr);
    saved = persistedNode(kPeer);
    TEST_ASSERT_TRUE(saved.has_value());
    TEST_ASSERT_TRUE(nodeInfoLiteHeardOnSlot(&*saved, otherSlot));
}

void setUp(void)
{
    scriptRail({});
}

void tearDown(void)
{
    Time::useRealClock();
}

NSR_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    nodeDB = new NodeDB(); // first boot on the pristine per-suite sandbox persists the default set

    UNITY_BEGIN();

    printf("\n=== saveProto ===\n");
    RUN_TEST(test_saveProto_failedRename_returnsFalse);

#if !defined(_WIN32)
    printf("\n=== saveToDisk retry gate ===\n");
    RUN_TEST(test_saveToDisk_railDipDuringWrite_retriesAndLands);
    RUN_TEST(test_saveToDisk_railStillUnsafeAtRetry_bailsWithoutFormat);
    RUN_TEST(test_saveToDisk_writeFailsButFsReadable_doesNotFormat);
    RUN_TEST(test_saveToDisk_railUnsafeAtEntry_returnsFalseImmediately);
#endif

    printf("\n=== deferred node database save ===\n");
    RUN_TEST(test_saveNodeDatabaseIfDirty_waitsOutThrottleThenSaves);
    RUN_TEST(test_rebootObserver_savesDirtyNodeDatabase);
    RUN_TEST(test_updateUser_deferredPeer_isPersistedAfterThrottle);
    RUN_TEST(test_updateUser_deferredPeer_isPersistedOnReboot);
    RUN_TEST(test_updateFrom_rfSlotChanges_arePersisted);

    exit(UNITY_END());
}

NSR_TEST_ENTRY void loop() {}

#else // !FSCom

void setUp(void) {}
void tearDown(void) {}

NSR_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

NSR_TEST_ENTRY void loop() {}

#endif
