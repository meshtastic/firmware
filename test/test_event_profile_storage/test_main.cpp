#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include <unity.h>

void test_standard_profile_uses_standard_files()
{
    const auto paths = radioProfileStoragePaths(false);

    TEST_ASSERT_EQUAL_STRING("/prefs/config.proto", paths.config);
    TEST_ASSERT_EQUAL_STRING("/prefs/channels.proto", paths.channels);
    TEST_ASSERT_EQUAL_STRING("/backups/backup.proto", paths.backup);
}

void test_event_profile_uses_isolated_files()
{
    const auto paths = radioProfileStoragePaths(true);

    TEST_ASSERT_EQUAL_STRING("/prefs/event-config.proto", paths.config);
    TEST_ASSERT_EQUAL_STRING("/prefs/event-channels.proto", paths.channels);
    TEST_ASSERT_EQUAL_STRING("/backups/event-backup.proto", paths.backup);
}

void test_boot_defers_persistence_until_config_is_verified()
{
    TEST_ASSERT_TRUE(shouldDeferBootPersistence(true, false, false));
    TEST_ASSERT_TRUE(shouldDeferBootPersistence(true, true, true));
    TEST_ASSERT_FALSE(shouldDeferBootPersistence(true, true, false));
    TEST_ASSERT_FALSE(shouldDeferBootPersistence(false, true, true));
}

void test_event_profile_requires_room_for_atomic_profile_writes()
{
    TEST_ASSERT_TRUE(hasEventProfileStorageSpace(EVENT_PROFILE_STORAGE_RESERVATION_BYTES, 0));
    TEST_ASSERT_FALSE(hasEventProfileStorageSpace(EVENT_PROFILE_STORAGE_RESERVATION_BYTES - 1, 0));
    TEST_ASSERT_FALSE(hasEventProfileStorageSpace(EVENT_PROFILE_STORAGE_RESERVATION_BYTES, 1));
    TEST_ASSERT_FALSE(hasEventProfileStorageSpace(1, 2));
    // A full filesystem must never look like it has room.
    TEST_ASSERT_FALSE(
        hasEventProfileStorageSpace(EVENT_PROFILE_STORAGE_RESERVATION_BYTES, EVENT_PROFILE_STORAGE_RESERVATION_BYTES));
}

// The whole point of the feature is that an event build never touches the user's real radio profile
// or the shared files. A copy/paste slip in the path table would silently overwrite them, which no
// amount of runtime testing would catch on a device that had never held a normal profile.
void test_event_paths_never_collide_with_standard_or_shared_files()
{
    const auto standard = radioProfileStoragePaths(false);
    const auto event = radioProfileStoragePaths(true);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.config, standard.config));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.channels, standard.channels));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.backup, standard.backup));

    // The three event paths must also be distinct from each other.
    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.config, event.channels));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.config, event.backup));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(event.channels, event.backup));

    // Files shared by both profiles must never be captured by either profile's paths.
    const char *sharedFiles[] = {deviceStateFileName, nodeDatabaseFileName, moduleConfigFileName, uiconfigFileName,
                                 legacyPrefFileName};
    for (const char *shared : sharedFiles) {
        TEST_ASSERT_NOT_EQUAL(0, strcmp(event.config, shared));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(event.channels, shared));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(event.backup, shared));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(standard.config, shared));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(standard.channels, shared));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(standard.backup, shared));
    }
}

// Boot-write deferral must cover the radio profile and nothing else: widening it silently drops
// boot-time recovery writes (devicestate/nodedb) that are never retried.
void test_only_radio_profile_files_defer_boot_writes()
{
    TEST_ASSERT_TRUE(isRadioProfileFile(configFileName));
    TEST_ASSERT_TRUE(isRadioProfileFile(channelFileName));
    TEST_ASSERT_TRUE(isRadioProfileFile(backupFileName));

    TEST_ASSERT_FALSE(isRadioProfileFile(deviceStateFileName));
    TEST_ASSERT_FALSE(isRadioProfileFile(nodeDatabaseFileName));
    TEST_ASSERT_FALSE(isRadioProfileFile(moduleConfigFileName));
    TEST_ASSERT_FALSE(isRadioProfileFile(uiconfigFileName));
}

// The reservation is compile-time, but the filesystems it has to fit in are tiny: nRF52840
// InternalFS is 28 KiB and the STM32WL family is 14 KiB. If protobuf growth pushes the reservation
// past what those can spare, event builds silently stop persisting their profile.
void test_event_reservation_fits_smallest_supported_filesystem()
{
    constexpr size_t STM32WL_FILESYSTEM_BYTES = 14 * 1024;
    constexpr size_t NRF52840_FILESYSTEM_BYTES = 28 * 1024;

    TEST_ASSERT_EQUAL_UINT32(2 * (meshtastic_LocalConfig_size + meshtastic_ChannelFile_size),
                             EVENT_PROFILE_STORAGE_RESERVATION_BYTES);

    // Leave at least half of the smallest filesystem for everything else under /prefs.
    TEST_ASSERT_LESS_THAN_UINT32(STM32WL_FILESYSTEM_BYTES / 2, EVENT_PROFILE_STORAGE_RESERVATION_BYTES);
    TEST_ASSERT_TRUE(hasEventProfileStorageSpace(NRF52840_FILESYSTEM_BYTES, 0));
    TEST_ASSERT_TRUE(hasEventProfileStorageSpace(STM32WL_FILESYSTEM_BYTES, 0));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_standard_profile_uses_standard_files);
    RUN_TEST(test_event_profile_uses_isolated_files);
    RUN_TEST(test_boot_defers_persistence_until_config_is_verified);
    RUN_TEST(test_event_profile_requires_room_for_atomic_profile_writes);
    RUN_TEST(test_event_paths_never_collide_with_standard_or_shared_files);
    RUN_TEST(test_only_radio_profile_files_defer_boot_writes);
    RUN_TEST(test_event_reservation_fits_smallest_supported_filesystem);
    exit(UNITY_END());
}

void loop() {}
}
