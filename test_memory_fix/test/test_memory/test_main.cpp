#include <cstdio>
#include <cstring>
#include <unity.h>
#include <vector>

struct meshtastic_FileInfo {
    char file_name[256];
    uint32_t size_bytes;
};

std::vector<meshtastic_FileInfo> mock_getFiles(const char *dirname, uint8_t levels, size_t max_files = 50)
{
    std::vector<meshtastic_FileInfo> files;
    files.reserve(std::min((size_t)32, max_files));

    if (strcmp(dirname, "/nonexistent") == 0) {
        return files;
    }

    size_t file_count = std::min((size_t)10, max_files);
    for (size_t i = 0; i < file_count && files.size() < max_files; i++) {
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

void test_file_limit()
{
    std::vector<meshtastic_FileInfo> files = mock_getFiles("/", 10, 50);
    TEST_ASSERT_TRUE(files.size() <= 50);
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
