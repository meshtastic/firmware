// Covers SafeFile's write-tmp / readback / rename-over path, on both the fullAtomic and the
// !fullAtomic construction. See the commit message for why these cannot go red on this host.
#include "MeshTypes.h"
#include "TestUtil.h"
// FSCommon.h is what defines FSCom, so it has to be included before anything tests for it.
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include <cstring>
#include <string>
#include <unity.h>

#ifdef FSCom

// Built through FSCom rather than host fopen(): PortduinoFS confines paths to its own mountpoint, so
// the same API is the only way the test stays agnostic about where that mountpoint is.
static const char *kRoot = "/test_safefile";
static const char *kFile = "/test_safefile/pref.dat";
static const char *kTmp = "/test_safefile/pref.dat.tmp";

static void writeRaw(const char *path, const char *payload)
{
    File f = FSCom.open(path, FILE_O_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, path);
    f.write((const uint8_t *)payload, strlen(payload));
    f.close();
}

static std::string readAll(const char *path)
{
    std::string out;
    File f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return out;
    int c;
    while ((c = f.read()) >= 0)
        out.push_back((char)c);
    f.close();
    return out;
}

static void saveThrough(bool fullAtomic, const char *payload)
{
    auto f = SafeFile(kFile, fullAtomic);
    f.write((const uint8_t *)payload, strlen(payload));
    TEST_ASSERT_TRUE(f.close());
}

void setUp(void)
{
    rmDir(kRoot);
    FSCom.mkdir(kRoot);
}

void tearDown(void)
{
    // Leave nothing behind: the sandbox is per suite, but an undeclared write is still a finding.
    rmDir(kRoot);
}

// A .tmp surviving a reset between close() and renameFile() must not contribute a byte to the
// next save.
void test_stale_tmp_is_not_appended_to(void)
{
    writeRaw(kTmp, "STALESTALESTALE");

    saveThrough(true, "NEW");

    TEST_ASSERT_EQUAL_STRING("NEW", readAll(kFile).c_str());
}

// fullAtomic decides whether the real file is nuked up front; it has no bearing on the .tmp, which
// both paths open with the same FILE_O_WRITE. This is why the remove must not be scoped to it.
void test_stale_tmp_is_not_appended_to_when_not_full_atomic(void)
{
    writeRaw(kTmp, "STALESTALESTALE");

    saveThrough(false, "NEW");

    TEST_ASSERT_EQUAL_STRING("NEW", readAll(kFile).c_str());
}

// A stale .tmp must not survive the save either, or the next one inherits it again.
void test_completed_save_leaves_no_tmp(void)
{
    writeRaw(kTmp, "STALE");

    saveThrough(true, "NEW");

    TEST_ASSERT_FALSE(FSCom.exists(kTmp));
}

// The rename is an overwrite, not an append onto the previous generation of the real file.
void test_save_replaces_previous_contents(void)
{
    writeRaw(kFile, "OLDOLDOLDOLDOLD");

    saveThrough(true, "NEW");

    TEST_ASSERT_EQUAL_STRING("NEW", readAll(kFile).c_str());
}

// The premise of the suite, asserted rather than assumed: this host truncates on FILE_O_WRITE.
// Portduino only, since it is false by design on the append-on-write backends the fix is aimed at.
#ifdef ARCH_PORTDUINO
void test_write_open_truncates_on_this_host(void)
{
    writeRaw(kFile, "AAAAAAAA");
    writeRaw(kFile, "B");

    TEST_ASSERT_EQUAL_STRING("B", readAll(kFile).c_str());
}
#endif

#endif // FSCom

void setup()
{
    initializeTestEnvironment();
    // SafeFile takes spiLock on every open; main.cpp does this, and nothing in the test harness does.
    initSPI();
    UNITY_BEGIN();
#ifdef FSCom
    RUN_TEST(test_stale_tmp_is_not_appended_to);
    RUN_TEST(test_stale_tmp_is_not_appended_to_when_not_full_atomic);
    RUN_TEST(test_completed_save_leaves_no_tmp);
    RUN_TEST(test_save_replaces_previous_contents);
#ifdef ARCH_PORTDUINO
    RUN_TEST(test_write_open_truncates_on_this_host);
#endif
#endif
    exit(UNITY_END());
}

void loop() {}
