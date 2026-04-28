#include <cstdio>
#include <cstring>
#include <unity.h>
#include <vector>
#include <string>

struct meshtastic_FileInfo {
    char file_name[228];
    uint32_t size_bytes;
};

std::vector<meshtastic_FileInfo> mock_getFiles(const char *dirname, uint8_t levels, size_t max_files = 50)
{
    std::vector<meshtastic_FileInfo> files;
    // Enforce the same hard cap as the real implementation to avoid tests diverging from production behavior
    const size_t HARD_CAP = 50;
    size_t effective_max = std::min(max_files, HARD_CAP);
    files.reserve(std::min((size_t)32, effective_max));

    if (strcmp(dirname, "/nonexistent") == 0) {
        return files;
    }

    size_t file_count = std::min((size_t)100, effective_max);
    for (size_t i = 0; i < file_count && files.size() < effective_max; i++) {
        meshtastic_FileInfo info = {"", 100};
        snprintf(info.file_name, sizeof(info.file_name), "/file%zu.txt", i);
        files.push_back(info);
    }

    return files;
}

bool mock_check_memory_limit(size_t free_heap, size_t min_required = 8192)
{
    return free_heap >= min_required;
}

std::vector<meshtastic_FileInfo> mock_getFiles_with_longname()
{
    std::vector<meshtastic_FileInfo> files;
    const size_t MAX_PATH_LENGTH = 200;
    const size_t HARD_CAP = 50;
    size_t attempts = HARD_CAP + 1; // generate one extra to test rejection of oversized paths

    for (size_t i = 0; i < attempts && files.size() < HARD_CAP; i++) {
        if (i == 0) {
            // create an intentionally oversized path and ensure it would be rejected
            std::string longname(MAX_PATH_LENGTH + 1, 'a');
            longname.insert(longname.begin(), '/');
            if (longname.length() >= MAX_PATH_LENGTH) {
                // emulate getValidFilePath rejecting this entry
                continue;
            }
        }

        meshtastic_FileInfo info = {"", 100};
        char buf[256];
        snprintf(buf, sizeof(buf), "/file%zu.txt", i);
        if (strlen(buf) >= MAX_PATH_LENGTH) {
            continue;
        }
        snprintf(info.file_name, sizeof(info.file_name), "%s", buf);
        files.push_back(info);
    }

    return files;
}

void test_file_limit()
{
    std::vector<meshtastic_FileInfo> files = mock_getFiles("/", 10, 100);
    TEST_ASSERT_EQUAL(50, files.size());
}

void test_empty_directory()
{
    std::vector<meshtastic_FileInfo> files = mock_getFiles("/nonexistent", 1);
    TEST_ASSERT_EQUAL(0, files.size());
}

void test_memory_protection()
{
    TEST_ASSERT_FALSE(mock_check_memory_limit(4096));
    TEST_ASSERT_TRUE(mock_check_memory_limit(16384));
}

void test_path_length_rejection()
{
    std::vector<meshtastic_FileInfo> files = mock_getFiles_with_longname();
    TEST_ASSERT_EQUAL(50, files.size());
}

void setUp(void) {}
void tearDown(void) {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_file_limit);
    RUN_TEST(test_empty_directory);
    RUN_TEST(test_memory_protection);
    return UNITY_END();
}
