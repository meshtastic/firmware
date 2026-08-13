// Tests for getFiles() - the bounded file-manifest walk in src/FSCommon.cpp that PhoneAPI's
// STATE_SEND_FILEMANIFEST drives on every phone sync. Nothing else asserts its bounding behaviour:
// the walk does run unasserted from test_stream_api's handshakes, but no test covers the cap, the
// depth limit, the wasLimited paths, overlong-path rejection, or capacity release.
//
// These assert what the code does today. Today's code is already correct here - #10778 landed the
// by-reference collectFiles(), the 64-entry cap, strlcpy bounds and the swap-idiom release - so all
// of these describe present behaviour rather than a pending fix.
#include "MeshTypes.h"
#include "TestUtil.h"
// FSCommon.h is what defines FSCom, so it has to be included before anything tests for it.
#include "FSCommon.h"
#include <cstdio>
#include <cstring>
#include <unity.h>
#include <vector>

#ifdef FSCom

// The suite builds its tree through FSCom rather than host mkdir/fopen: PortduinoFS confines paths
// to its mountpoint, so going through the same API is the only way the test stays agnostic about
// where that mountpoint is.
static const char *kRoot = "/test_getfiles";

static void makeFile(const char *path, size_t bytes)
{
    File f = FSCom.open(path, FILE_O_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, path);
    for (size_t i = 0; i < bytes; i++)
        f.write('x');
    f.close();
}

static bool manifestContains(const std::vector<meshtastic_FileInfo> &files, const char *suffix)
{
    for (const auto &f : files) {
        const size_t nameLen = strlen(f.file_name);
        const size_t suffixLen = strlen(suffix);
        if (nameLen >= suffixLen && strcmp(f.file_name + nameLen - suffixLen, suffix) == 0)
            return true;
    }
    return false;
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

// 1. The cap is the fix from #10778 that this suite exists to pin.
void test_getfiles_respects_max_count(void)
{
    for (int i = 0; i < 80; i++) {
        char p[64];
        snprintf(p, sizeof(p), "%s/f%02d.txt", kRoot, i);
        makeFile(p, 4);
    }

    bool limited = false;
    auto files = getFiles(kRoot, 1, 64, &limited);
    TEST_ASSERT_EQUAL_size_t(64, files.size());
    TEST_ASSERT_TRUE(limited);
}

// 2. wasLimited must not be sticky - it is an out-param, initialised per call.
void test_getfiles_unlimited_when_under_cap(void)
{
    for (int i = 0; i < 5; i++) {
        char p[64];
        snprintf(p, sizeof(p), "%s/small%d.txt", kRoot, i);
        makeFile(p, 4);
    }

    bool limited = true; // deliberately pre-set: the call must clear it
    auto files = getFiles(kRoot, 1, 64, &limited);
    TEST_ASSERT_EQUAL_size_t(5, files.size());
    TEST_ASSERT_FALSE(limited);
}

// 3. Depth: a file below the requested level is absent AND reported as a truncation.
void test_getfiles_depth_limit(void)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/a", kRoot);
    FSCom.mkdir(dir);
    snprintf(dir, sizeof(dir), "%s/a/b", kRoot);
    FSCom.mkdir(dir);
    snprintf(dir, sizeof(dir), "%s/a/b/c", kRoot);
    FSCom.mkdir(dir);

    char deep[160];
    snprintf(deep, sizeof(deep), "%s/a/b/c/deep.txt", kRoot);
    makeFile(deep, 8);

    bool shallowLimited = false;
    auto shallow = getFiles(kRoot, 2, 64, &shallowLimited);
    TEST_ASSERT_FALSE(manifestContains(shallow, "deep.txt"));
    TEST_ASSERT_TRUE(shallowLimited);

    bool deepLimited = false;
    auto deepFiles = getFiles(kRoot, 4, 64, &deepLimited);
    TEST_ASSERT_TRUE(manifestContains(deepFiles, "deep.txt"));
    TEST_ASSERT_FALSE(deepLimited);
}

// 4. A path that will not fit meshtastic_FileInfo::file_name is dropped, not truncated into the
//    manifest, and the drop is reported.
void test_getfiles_rejects_overlong_path(void)
{
    // file_name is 228 bytes; build a nested path that overruns it while each component stays
    // inside the host's 255-byte limit.
    char dir[512];
    strcpy(dir, kRoot);
    for (int level = 0; level < 3; level++) {
        char component[80];
        memset(component, 'd', sizeof(component) - 1);
        component[sizeof(component) - 1] = '\0';
        snprintf(dir + strlen(dir), sizeof(dir) - strlen(dir), "/%s", component);
        FSCom.mkdir(dir);
    }

    char longFile[600];
    snprintf(longFile, sizeof(longFile), "%s/overlong.txt", dir);
    makeFile(longFile, 4);

    char normal[128];
    snprintf(normal, sizeof(normal), "%s/normal.txt", kRoot);
    makeFile(normal, 4);

    bool limited = false;
    auto files = getFiles(kRoot, 5, 64, &limited);
    TEST_ASSERT_TRUE(manifestContains(files, "normal.txt"));
    TEST_ASSERT_FALSE(manifestContains(files, "overlong.txt"));
    TEST_ASSERT_TRUE(limited);

    // rmDir() cannot reach this tree - it walks through the same 228-byte path buffer that made the
    // file unlistable in the first place - so unwind it here, deepest first.
    FSCom.remove(longFile);
    for (int level = 0; level < 3; level++) {
        FSCom.rmdir(dir);
        *strrchr(dir, '/') = '\0';
    }
}

// 5. pathEndsWithDot() - no entry in the manifest may end in '.', which is how the walk filters the
//    "." and ".." pseudo-entries some backends return.
void test_getfiles_skips_dot_entries(void)
{
    char plain[128];
    snprintf(plain, sizeof(plain), "%s/plain.txt", kRoot);
    makeFile(plain, 4);

    char trailingDot[128];
    snprintf(trailingDot, sizeof(trailingDot), "%s/trailing.", kRoot);
    makeFile(trailingDot, 4);

    bool limited = false;
    auto files = getFiles(kRoot, 1, 64, &limited);
    TEST_ASSERT_TRUE(manifestContains(files, "plain.txt"));
    for (const auto &f : files) {
        const size_t len = strlen(f.file_name);
        TEST_ASSERT_TRUE_MESSAGE(len == 0 || f.file_name[len - 1] != '.', f.file_name);
    }
}

// 6. size_bytes is populated at all - the only coverage that the struct carries more than a name.
void test_getfiles_reports_sizes(void)
{
    char p[128];
    snprintf(p, sizeof(p), "%s/sized.txt", kRoot);
    makeFile(p, 137);

    bool limited = false;
    auto files = getFiles(kRoot, 1, 64, &limited);

    bool found = false;
    for (const auto &f : files) {
        if (strstr(f.file_name, "sized.txt")) {
            TEST_ASSERT_EQUAL_UINT32(137, f.size_bytes);
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// 7. A directory that does not exist is empty and untruncated, not a crash.
void test_getfiles_missing_dir_is_empty(void)
{
    bool limited = true;
    auto files = getFiles("/test_getfiles_does_not_exist", 3, 64, &limited);
    TEST_ASSERT_EQUAL_size_t(0, files.size());
    TEST_ASSERT_FALSE(limited);
}

// 8. Capacity release. PhoneAPI's releaseFilesManifest() is file-local, so this pins the idiom it
//    uses rather than calling it: a manifest reserved for 64 entries holds ~14 KB of file_name
//    buffers, and clear() returns none of it. A size-only assertion would pass on clear(), which is
//    exactly the bug #7924 shipped.
void test_release_files_manifest_frees_capacity(void)
{
    std::vector<meshtastic_FileInfo> manifest;
    manifest.reserve(64);
    for (int i = 0; i < 64; i++) {
        meshtastic_FileInfo info = {"", 0};
        snprintf(info.file_name, sizeof(info.file_name), "/f%02d.txt", i);
        manifest.push_back(info);
    }
    TEST_ASSERT_EQUAL_size_t(64, manifest.size());
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(64, manifest.capacity());

    std::vector<meshtastic_FileInfo>().swap(manifest);

    TEST_ASSERT_EQUAL_size_t(0, manifest.size());
    TEST_ASSERT_EQUAL_size_t(0, manifest.capacity());
}

#endif // FSCom

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
#ifdef FSCom
    RUN_TEST(test_getfiles_respects_max_count);
    RUN_TEST(test_getfiles_unlimited_when_under_cap);
    RUN_TEST(test_getfiles_depth_limit);
    RUN_TEST(test_getfiles_rejects_overlong_path);
    RUN_TEST(test_getfiles_skips_dot_entries);
    RUN_TEST(test_getfiles_reports_sizes);
    RUN_TEST(test_getfiles_missing_dir_is_empty);
    RUN_TEST(test_release_files_manifest_frees_capacity);
#endif
    exit(UNITY_END());
}

void loop() {}
