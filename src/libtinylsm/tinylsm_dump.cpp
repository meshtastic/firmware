#include "tinylsm_dump.h"
#include "configuration.h"
#include "tinylsm_adapter.h"
#include "tinylsm_fs.h"

namespace meshtastic
{
namespace tinylsm
{

size_t LSMDumpManager::dumpForFirmwareUpdate()
{
    LOG_INFO("LSM DUMP: Preparing for firmware update - clearing LSM storage to free flash space");

    size_t bytes_before = getFlashUsage();

    // Flush any pending data first
    if (g_nodedb_adapter) {
        LOG_INFO("LSM DUMP: Flushing pending writes...");
        g_nodedb_adapter->flush();
    }

    // Delete all SortedTable files
    size_t deleted = 0;

    // Delete durable SortedTables
    const char *durable_path = "/lfs/nodedb_d";
    if (FileSystem::exists(durable_path)) {
        LOG_INFO("LSM DUMP: Removing durable SortedTables from %s", durable_path);

        auto callback = [](const char *filename, void *user_data) {
            size_t *count = static_cast<size_t *>(user_data);
            if (strstr(filename, ".sst") != nullptr) {
                char filepath[constants::MAX_PATH];
                snprintf(filepath, sizeof(filepath), "/lfs/nodedb_d/%s", filename);
                if (FileSystem::remove(filepath)) {
                    (*count)++;
                    LOG_DEBUG("LSM DUMP: Deleted %s", filename);
                }
            }
        };

        FileSystem::list_files(durable_path, callback, &deleted);
    }

    // Delete ephemeral SortedTables
    const char *ephemeral_path = "/lfs/nodedb_e";
    if (FileSystem::exists(ephemeral_path)) {
        LOG_INFO("LSM DUMP: Removing ephemeral SortedTables from %s", ephemeral_path);

        auto callback = [](const char *filename, void *user_data) {
            size_t *count = static_cast<size_t *>(user_data);
            if (strstr(filename, ".sst") != nullptr) {
                char filepath[constants::MAX_PATH];
                snprintf(filepath, sizeof(filepath), "/lfs/nodedb_e/%s", filename);
                if (FileSystem::remove(filepath)) {
                    (*count)++;
                    LOG_DEBUG("LSM DUMP: Deleted %s", filename);
                }
            }
        };

        FileSystem::list_files(ephemeral_path, callback, &deleted);
    }

    size_t bytes_after = getFlashUsage();
    size_t bytes_freed = bytes_before - bytes_after;

    LOG_INFO("LSM DUMP: Complete - deleted %u SortedTables, freed ~%u KB", deleted, bytes_freed / 1024);

    return bytes_freed;
}

bool LSMDumpManager::shouldDump()
{
#if defined(ARCH_NRF52)
    // On nRF52, dump if USB is connected (need space for DFU)
    if (Serial) {
        LOG_INFO("LSM DUMP: USB detected on nRF52, should dump LSM to free flash");
        return true;
    }
#endif

    // Check if flash is critically low
    size_t free_space = FileSystem::free_space();
    if (free_space < 100 * 1024) { // Less than 100KB free
        LOG_WARN("LSM DUMP: Flash critically low (%u KB free), should dump LSM", free_space / 1024);
        return true;
    }

    return false;
}

bool LSMDumpManager::clearAll()
{
    LOG_WARN("LSM DUMP: CLEARING ALL LSM DATA (emergency recovery)");

    // Remove all LSM directories
    FileSystem::remove("/lfs/nodedb_d");
    FileSystem::remove("/lfs/nodedb_e");

    LOG_INFO("LSM DUMP: All LSM data cleared");
    return true;
}

size_t LSMDumpManager::getFlashUsage()
{
    size_t total = 0;

    // Count durable files
    auto count_callback = [](const char *filename, void *user_data) {
        size_t *total_size = static_cast<size_t *>(user_data);
        char filepath[constants::MAX_PATH];

        // Durable path
        snprintf(filepath, sizeof(filepath), "/lfs/nodedb_d/%s", filename);
        if (FileSystem::exists(filepath)) {
            // Approximate size (would need to open file to get exact size)
            *total_size += 10 * 1024; // Estimate 10KB per file
        }

        // Ephemeral path
        snprintf(filepath, sizeof(filepath), "/lfs/nodedb_e/%s", filename);
        if (FileSystem::exists(filepath)) {
            *total_size += 10 * 1024;
        }
    };

    if (FileSystem::exists("/lfs/nodedb_d")) {
        FileSystem::list_files("/lfs/nodedb_d", count_callback, &total);
    }
    if (FileSystem::exists("/lfs/nodedb_e")) {
        FileSystem::list_files("/lfs/nodedb_e", count_callback, &total);
    }

    return total;
}

} // namespace tinylsm
} // namespace meshtastic
