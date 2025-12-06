/**
 * @file meshstatic_storage.c
 * @brief Implementation of flash storage manager for CSV batches
 *
 * POSIX Implementation Notes:
 * - Uses standard FILE* operations (fopen, fwrite, etc.)
 * - For RP2350 LittleFS: Replace with lfs_file_open, lfs_file_write, etc.
 * - Directory operations: mkdir() → lfs_mkdir()
 * - File listing: opendir()/readdir() → lfs_dir_open()/lfs_dir_read()
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_storage.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================================================================
 * Platform Abstraction Layer (PAL)
 * ============================================================================ */

/* POSIX implementation - for testing on desktop */
/* For RP2350, replace these with LittleFS equivalents */

#define STORAGE_ROOT "."  /* Current directory for testing */

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Create storage directory if it doesn't exist
 */
static bool create_storage_dir(void)
{
    char path[128];
    snprintf(path, sizeof(path), "%s%s", STORAGE_ROOT, MESHSTATIC_STORAGE_DIR);

    /* Check if directory exists */
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;  /* Already exists */
    }

    /* Create directory */
    #ifdef _WIN32
    if (mkdir(path) != 0) {
    #else
    if (mkdir(path, 0755) != 0) {
    #endif
        return false;
    }

    return true;
}

/**
 * @brief Count existing batch files in storage
 */
static uint32_t count_batch_files(void)
{
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s%s", STORAGE_ROOT, MESHSTATIC_STORAGE_DIR);

    DIR* dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    uint32_t count = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Check if filename matches pattern: batch_XXXXX.csv */
        if (strncmp(entry->d_name, MESHSTATIC_FILE_PREFIX, strlen(MESHSTATIC_FILE_PREFIX)) == 0 &&
            strstr(entry->d_name, MESHSTATIC_FILE_EXT) != NULL) {
            count++;
        }
    }

    closedir(dir);
    return count;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

void meshstatic_storage_format_filename(uint32_t batch_id, char* filename)
{
    if (!filename) return;

    snprintf(filename, MESHSTATIC_MAX_FILENAME, "%s%05u%s",
             MESHSTATIC_FILE_PREFIX, batch_id, MESHSTATIC_FILE_EXT);
}

void meshstatic_storage_get_full_path(uint32_t batch_id, char* path)
{
    if (!path) return;

    char filename[MESHSTATIC_MAX_FILENAME];
    meshstatic_storage_format_filename(batch_id, filename);

    snprintf(path, 128, "%s%s/%s", STORAGE_ROOT, MESHSTATIC_STORAGE_DIR, filename);
}

meshstatic_storage_init_result_t meshstatic_storage_init(void)
{
    meshstatic_storage_init_result_t result = {0};

    /* Create storage directory */
    if (!create_storage_dir()) {
        result.success = false;
        snprintf(result.error_msg, sizeof(result.error_msg),
                "Failed to create directory: %s", strerror(errno));
        return result;
    }

    /* Count existing batches (for recovery) */
    result.recovered_batches = count_batch_files();

    result.success = true;
    return result;
}

bool meshstatic_storage_save_batch(const meshstatic_batch_t* batch)
{
    if (!batch) return false;

    /* Get CSV data */
    const char* csv = meshstatic_batch_get_csv(batch);
    uint32_t length = meshstatic_batch_get_csv_length(batch);

    if (!csv || length == 0) return false;

    /* Verify 200-byte limit */
    if (length > MESHSTATIC_MAX_BATCH_SIZE) {
        return false;  /* Batch too large */
    }

    /* Generate file path */
    char path[128];
    meshstatic_storage_get_full_path(batch->meta.batch_id, path);

    /* Open file for writing */
    FILE* file = fopen(path, "w");
    if (!file) {
        return false;
    }

    /* Write CSV data */
    size_t written = fwrite(csv, 1, length, file);
    fclose(file);

    return (written == length);
}

bool meshstatic_storage_load_batch(uint32_t batch_id, meshstatic_batch_t* batch)
{
    if (!batch) return false;

    /* Generate file path */
    char path[128];
    meshstatic_storage_get_full_path(batch_id, path);

    /* Open file for reading */
    FILE* file = fopen(path, "r");
    if (!file) {
        return false;
    }

    /* Read entire file into buffer */
    char csv_buffer[MESHSTATIC_MAX_BATCH_SIZE + 1];
    size_t bytes_read = fread(csv_buffer, 1, MESHSTATIC_MAX_BATCH_SIZE, file);
    fclose(file);

    if (bytes_read == 0) {
        return false;
    }

    /* Null-terminate */
    csv_buffer[bytes_read] = '\0';

    /* Initialize batch structure */
    meshstatic_batch_init(batch);

    /* Parse CSV and reconstruct batch */
    /* For now, just copy CSV buffer (Component 3 will handle parsing) */
    strncpy(batch->csv_buffer, csv_buffer, MESHSTATIC_CSV_BUFFER_SIZE);
    batch->meta.csv_length = bytes_read;
    batch->meta.batch_id = batch_id;

    /* TODO: Parse CSV rows back into keystroke array (if needed) */

    return true;
}

bool meshstatic_storage_delete_batch(uint32_t batch_id)
{
    char path[128];
    meshstatic_storage_get_full_path(batch_id, path);

    return (remove(path) == 0);
}

bool meshstatic_storage_batch_exists(uint32_t batch_id)
{
    char path[128];
    meshstatic_storage_get_full_path(batch_id, path);

    FILE* file = fopen(path, "r");
    if (file) {
        fclose(file);
        return true;
    }

    return false;
}

uint32_t* meshstatic_storage_list_batches(uint32_t* count)
{
    if (!count) return NULL;

    *count = 0;

    /* Open directory */
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s%s", STORAGE_ROOT, MESHSTATIC_STORAGE_DIR);

    DIR* dir = opendir(dir_path);
    if (!dir) {
        return NULL;
    }

    /* First pass: count batch files */
    uint32_t batch_count = 0;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, MESHSTATIC_FILE_PREFIX, strlen(MESHSTATIC_FILE_PREFIX)) == 0 &&
            strstr(entry->d_name, MESHSTATIC_FILE_EXT) != NULL) {
            batch_count++;
        }
    }

    if (batch_count == 0) {
        closedir(dir);
        return NULL;
    }

    /* Allocate array */
    uint32_t* batch_ids = (uint32_t*)malloc(batch_count * sizeof(uint32_t));
    if (!batch_ids) {
        closedir(dir);
        return NULL;
    }

    /* Second pass: extract batch IDs */
    rewinddir(dir);
    uint32_t idx = 0;

    while ((entry = readdir(dir)) != NULL && idx < batch_count) {
        if (strncmp(entry->d_name, MESHSTATIC_FILE_PREFIX, strlen(MESHSTATIC_FILE_PREFIX)) == 0 &&
            strstr(entry->d_name, MESHSTATIC_FILE_EXT) != NULL) {

            /* Parse batch ID from filename: batch_00001.csv → 1 */
            uint32_t batch_id;
            if (sscanf(entry->d_name, "batch_%u.csv", &batch_id) == 1) {
                batch_ids[idx++] = batch_id;
            }
        }
    }

    closedir(dir);

    /* Sort batch IDs in ascending order (simple bubble sort) */
    for (uint32_t i = 0; i < idx - 1; i++) {
        for (uint32_t j = 0; j < idx - i - 1; j++) {
            if (batch_ids[j] > batch_ids[j + 1]) {
                uint32_t temp = batch_ids[j];
                batch_ids[j] = batch_ids[j + 1];
                batch_ids[j + 1] = temp;
            }
        }
    }

    *count = idx;
    return batch_ids;
}

void meshstatic_storage_get_stats(meshstatic_storage_stats_t* stats)
{
    if (!stats) return;

    memset(stats, 0, sizeof(meshstatic_storage_stats_t));

    /* Get list of batches */
    uint32_t count = 0;
    uint32_t* batch_ids = meshstatic_storage_list_batches(&count);

    if (!batch_ids || count == 0) {
        return;
    }

    /* Calculate statistics */
    stats->total_batches = count;
    stats->oldest_batch_id = batch_ids[0];
    stats->newest_batch_id = batch_ids[count - 1];
    stats->storage_full = (count >= MESHSTATIC_MAX_BATCH_FILES);

    /* Calculate total bytes */
    for (uint32_t i = 0; i < count; i++) {
        char path[128];
        meshstatic_storage_get_full_path(batch_ids[i], path);

        FILE* file = fopen(path, "r");
        if (file) {
            fseek(file, 0, SEEK_END);
            stats->total_bytes += ftell(file);
            fclose(file);
        }
    }

    free(batch_ids);
}

uint32_t meshstatic_storage_cleanup_old(uint32_t count)
{
    uint32_t total_batches = 0;
    uint32_t* batch_ids = meshstatic_storage_list_batches(&total_batches);

    if (!batch_ids || total_batches == 0) {
        return 0;
    }

    /* Delete oldest N batches */
    uint32_t deleted = 0;
    uint32_t to_delete = (count < total_batches) ? count : total_batches;

    for (uint32_t i = 0; i < to_delete; i++) {
        if (meshstatic_storage_delete_batch(batch_ids[i])) {
            deleted++;
        }
    }

    free(batch_ids);
    return deleted;
}

char* meshstatic_storage_export_batch(uint32_t batch_id, uint32_t* length)
{
    if (!length) return NULL;

    *length = 0;

    /* Get file path */
    char path[128];
    meshstatic_storage_get_full_path(batch_id, path);

    /* Open file */
    FILE* file = fopen(path, "r");
    if (!file) {
        return NULL;
    }

    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MESHSTATIC_MAX_BATCH_SIZE) {
        fclose(file);
        return NULL;
    }

    /* Allocate buffer */
    char* csv_string = (char*)malloc(file_size + 1);
    if (!csv_string) {
        fclose(file);
        return NULL;
    }

    /* Read file */
    size_t bytes_read = fread(csv_string, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(csv_string);
        return NULL;
    }

    /* Null-terminate */
    csv_string[bytes_read] = '\0';

    *length = bytes_read;
    return csv_string;
}

uint32_t meshstatic_storage_get_next_to_transmit(void)
{
    uint32_t count = 0;
    uint32_t* batch_ids = meshstatic_storage_list_batches(&count);

    if (!batch_ids || count == 0) {
        return 0;
    }

    /* Return oldest batch ID */
    uint32_t next_id = batch_ids[0];
    free(batch_ids);

    return next_id;
}

void meshstatic_storage_mark_transmitted(uint32_t batch_id)
{
    /* In a full implementation, this would update metadata file */
    /* For now, this is a placeholder for future enhancement */
    (void)batch_id;  /* Suppress unused warning */

    /* TODO: Write transmission timestamp to metadata.txt */
}
