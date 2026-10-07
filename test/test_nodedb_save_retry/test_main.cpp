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

#include "mesh/NodeDB.h"
#include "power/PowerHAL.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

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

void setUp(void)
{
    scriptRail({});
}

void tearDown(void) {}

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
