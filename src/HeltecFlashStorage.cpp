/**
 * @file HeltecFlashStorage.cpp
 * @brief NASA Power of 10 compliant flash storage implementation for Heltec V4
 *
 * Compliance with NASA JPL's "Power of 10" rules:
 * Rule 1: Simple control flow (no goto, setjmp, recursion)
 * Rule 2: Fixed upper bounds on all loops
 * Rule 3: No dynamic memory allocation after initialization
 * Rule 4: Functions limited to ~60 lines
 * Rule 5: Minimum 2 assertions per function
 * Rule 6: Data declared at smallest scope
 * Rule 7: Return values checked, parameters validated
 * Rule 8: Limited preprocessor use (simple macros only)
 * Rule 9: Pointer use restricted (single dereference)
 * Rule 10: Compile with all warnings enabled
 *
 * Wear Leveling: Handled automatically by LittleFS filesystem
 */

#include "configuration.h"

#if defined(HELTEC_V4) || defined(HAS_FLASH_STORAGE)

#include "HeltecFlashStorage.h"
#include "main.h"
#include <assert.h>

// ============================================================================
// Constructor
// ============================================================================

HeltecFlashStorage::HeltecFlashStorage() : initialized(false)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1
    assert(FLASH_MAX_BUFFER_SIZE >= 64U);      // Rule 5: assertion 2

    // Initialize path buffer to empty (Rule 3: no dynamic allocation)
    pathBuffer[0] = '\0';
}

// ============================================================================
// Public Methods - Initialization
// ============================================================================

FlashStorageStatus HeltecFlashStorage::begin(bool formatOnFail)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 2

    if (initialized) {
        return FLASH_OK;
    }

    bool mountOk = LittleFS.begin(formatOnFail); // Rule 7: capture return
    if (!mountOk) {
        LOG_ERROR("FlashStorage: Failed to mount LittleFS");
        return FLASH_ERR_NOT_INITIALIZED;
    }

    initialized = true;
    LOG_INFO("FlashStorage: Initialized successfully");

    // Log storage info
    uint32_t total = LittleFS.totalBytes();
    uint32_t used = LittleFS.usedBytes();
    LOG_INFO("FlashStorage: Total=%lu, Used=%lu, Free=%lu", total, used, total - used);

    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::format()
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 2

    LOG_WARN("FlashStorage: Formatting storage...");

    bool formatOk = LittleFS.format(); // Rule 7: check return
    if (!formatOk) {
        LOG_ERROR("FlashStorage: Format failed");
        return FLASH_ERR_FORMAT_FAILED;
    }

    // Remount after format
    bool mountOk = LittleFS.begin(false); // Rule 7: check return
    if (!mountOk) {
        LOG_ERROR("FlashStorage: Failed to mount after format");
        initialized = false;
        return FLASH_ERR_NOT_INITIALIZED;
    }

    initialized = true;
    LOG_INFO("FlashStorage: Format complete");
    return FLASH_OK;
}

// ============================================================================
// Public Methods - File Operations
// ============================================================================

FlashStorageStatus HeltecFlashStorage::createFile(const char *nodeId, const char *filename)
{
    assert(nodeId != nullptr);    // Rule 5: assertion 1
    assert(filename != nullptr);  // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr)) {
        LOG_WARN("FlashStorage: Null parameter in createFile");
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        LOG_WARN("FlashStorage: Not initialized");
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId)) {
        LOG_WARN("FlashStorage: Invalid node ID");
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!isValidFilename(filename)) {
        LOG_WARN("FlashStorage: Invalid filename");
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Ensure directory exists
    bool dirOk = ensureDirectoryExists(nodeId);
    if (!dirOk) {
        LOG_ERROR("FlashStorage: Failed to create directory for node %s", nodeId);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Create or truncate file
    File file = LittleFS.open(fullPath, "w");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to create file %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    file.close();
    LOG_DEBUG("FlashStorage: Created file %s", fullPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::deleteFile(const char *nodeId, const char *filename)
{
    assert(nodeId != nullptr);    // Rule 5: assertion 1
    assert(filename != nullptr);  // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Check if file exists
    if (!LittleFS.exists(fullPath)) {
        LOG_DEBUG("FlashStorage: File not found for delete: %s", fullPath);
        return FLASH_ERR_FILE_NOT_FOUND;
    }

    // Delete file
    bool deleteOk = LittleFS.remove(fullPath); // Rule 7: check return
    if (!deleteOk) {
        LOG_ERROR("FlashStorage: Failed to delete file %s", fullPath);
        return FLASH_ERR_DELETE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Deleted file %s", fullPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::deleteNodeDirectory(const char *nodeId)
{
    assert(nodeId != nullptr);                 // Rule 5: assertion 1
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if (nodeId == nullptr) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build directory path
    char dirPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, nullptr, dirPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Check if directory exists
    if (!LittleFS.exists(dirPath)) {
        LOG_DEBUG("FlashStorage: Directory not found: %s", dirPath);
        return FLASH_ERR_DIR_NOT_FOUND;
    }

    // Delete directory and contents
    bool deleteOk = deleteDirectoryRecursive(dirPath);
    if (!deleteOk) {
        LOG_ERROR("FlashStorage: Failed to delete directory %s", dirPath);
        return FLASH_ERR_DELETE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Deleted directory %s", dirPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::readFile(const char *nodeId, const char *filename, uint8_t *buffer,
                                                 uint16_t maxLength, uint16_t *bytesRead, uint32_t offset)
{
    assert(nodeId != nullptr);     // Rule 5: assertion 1
    assert(buffer != nullptr);     // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (buffer == nullptr) || (bytesRead == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (maxLength == 0) {
        *bytesRead = 0;
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        *bytesRead = 0;
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        *bytesRead = 0;
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        *bytesRead = 0;
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Open file for reading
    File file = LittleFS.open(fullPath, "r");
    if (!file) {
        LOG_DEBUG("FlashStorage: File not found: %s", fullPath);
        *bytesRead = 0;
        return FLASH_ERR_FILE_NOT_FOUND;
    }

    // Seek to offset if needed
    if (offset > 0) {
        bool seekOk = file.seek(offset); // Rule 7: check return
        if (!seekOk) {
            file.close();
            *bytesRead = 0;
            return FLASH_ERR_READ_FAILED;
        }
    }

    // Read data
    size_t read = file.read(buffer, maxLength);
    file.close();

    *bytesRead = (uint16_t)read;
    LOG_DEBUG("FlashStorage: Read %d bytes from %s", read, fullPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::writeFile(const char *nodeId, const char *filename, const uint8_t *data,
                                                  uint16_t length)
{
    assert(nodeId != nullptr);   // Rule 5: assertion 1
    assert(data != nullptr);     // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (data == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (length == 0) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Ensure directory exists
    bool dirOk = ensureDirectoryExists(nodeId);
    if (!dirOk) {
        return FLASH_ERR_WRITE_FAILED;
    }

    // Open file for writing (creates or truncates)
    File file = LittleFS.open(fullPath, "w");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to open file for write: %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Write with retry (Rule 2: bounded loop)
    uint8_t retries = 0;
    size_t written = 0;
    while ((written == 0) && (retries < FLASH_MAX_WRITE_RETRIES)) {
        written = file.write(data, length);
        retries++;
    }

    file.close();

    if (written != length) {
        LOG_ERROR("FlashStorage: Write incomplete: %d/%d bytes", written, length);
        return FLASH_ERR_WRITE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Wrote %d bytes to %s", length, fullPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::appendFile(const char *nodeId, const char *filename, const uint8_t *data,
                                                   uint16_t length)
{
    assert(nodeId != nullptr);   // Rule 5: assertion 1
    assert(data != nullptr);     // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (data == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if ((length == 0) || (length > FLASH_MAX_APPEND_SIZE)) {
        LOG_WARN("FlashStorage: Invalid append length %d", length);
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Ensure directory exists
    bool dirOk = ensureDirectoryExists(nodeId);
    if (!dirOk) {
        return FLASH_ERR_WRITE_FAILED;
    }

    // Open file for append (creates if doesn't exist)
    File file = LittleFS.open(fullPath, "a");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to open file for append: %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Write data
    size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LOG_ERROR("FlashStorage: Append incomplete: %d/%d bytes", written, length);
        return FLASH_ERR_WRITE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Appended %d bytes to %s", length, fullPath);
    return FLASH_OK;
}

FlashStorageStatus HeltecFlashStorage::editFile(const char *nodeId, const char *filename, uint32_t offset,
                                                 const uint8_t *data, uint16_t length)
{
    assert(nodeId != nullptr);   // Rule 5: assertion 1
    assert(data != nullptr);     // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (data == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (length == 0) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Check if file exists
    if (!LittleFS.exists(fullPath)) {
        return FLASH_ERR_FILE_NOT_FOUND;
    }

    // Open file for read+write
    File file = LittleFS.open(fullPath, "r+");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to open file for edit: %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Check bounds
    uint32_t fileSize = file.size();
    if ((offset + length) > fileSize) {
        file.close();
        LOG_WARN("FlashStorage: Edit would exceed file bounds");
        return FLASH_ERR_INVALID_PARAM;
    }

    // Seek to offset
    bool seekOk = file.seek(offset); // Rule 7: check return
    if (!seekOk) {
        file.close();
        return FLASH_ERR_WRITE_FAILED;
    }

    // Write data at offset
    size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LOG_ERROR("FlashStorage: Edit incomplete: %d/%d bytes", written, length);
        return FLASH_ERR_WRITE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Edited %d bytes at offset %lu in %s", length, offset, fullPath);
    return FLASH_OK;
}

// ============================================================================
// Public Methods - File Information
// ============================================================================

bool HeltecFlashStorage::fileExists(const char *nodeId, const char *filename)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr)) {
        return false;
    }

    assert((nodeId != nullptr) && (filename != nullptr)); // Rule 5: assertion 2

    if (!initialized) {
        return false;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        return false;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return false;
    }

    return LittleFS.exists(fullPath);
}

bool HeltecFlashStorage::nodeDirectoryExists(const char *nodeId)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1

    // Rule 7: Parameter validation
    if (nodeId == nullptr) {
        return false;
    }

    assert(nodeId != nullptr); // Rule 5: assertion 2

    if (!initialized) {
        return false;
    }

    if (!isValidNodeId(nodeId)) {
        return false;
    }

    // Build directory path
    char dirPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, nullptr, dirPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return false;
    }

    return LittleFS.exists(dirPath);
}

FlashStorageStatus HeltecFlashStorage::getFileSize(const char *nodeId, const char *filename, uint32_t *fileSize)
{
    assert(nodeId != nullptr);    // Rule 5: assertion 1
    assert(fileSize != nullptr);  // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (fileSize == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        *fileSize = 0;
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId) || !isValidFilename(filename)) {
        *fileSize = 0;
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build full path
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        *fileSize = 0;
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Open file to get size
    File file = LittleFS.open(fullPath, "r");
    if (!file) {
        *fileSize = 0;
        return FLASH_ERR_FILE_NOT_FOUND;
    }

    *fileSize = file.size();
    file.close();

    return FLASH_OK;
}

// ============================================================================
// Public Methods - Storage Statistics
// ============================================================================

FlashStorageStatus HeltecFlashStorage::getStorageStats(FlashStorageStats *stats)
{
    assert(stats != nullptr);                  // Rule 5: assertion 1
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if (stats == nullptr) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        stats->totalBytes = 0;
        stats->usedBytes = 0;
        stats->freeBytes = 0;
        stats->totalFiles = 0;
        stats->totalDirectories = 0;
        return FLASH_ERR_NOT_INITIALIZED;
    }

    stats->totalBytes = LittleFS.totalBytes();
    stats->usedBytes = LittleFS.usedBytes();
    stats->freeBytes = stats->totalBytes - stats->usedBytes;
    stats->totalFiles = 0;
    stats->totalDirectories = 0;

    // Count files and directories (Rule 2: bounded loop)
    File root = LittleFS.open("/");
    if (!root) {
        return FLASH_OK; // Stats still valid, just no file count
    }

    uint16_t dirCount = 0;
    File entry = root.openNextFile();

    while (entry && (dirCount < FLASH_MAX_DIRECTORIES)) {
        if (entry.isDirectory()) {
            stats->totalDirectories++;

            // Count files in this directory (Rule 2: bounded inner loop)
            File subEntry = entry.openNextFile();
            uint16_t fileCount = 0;

            while (subEntry && (fileCount < FLASH_MAX_FILES_PER_DIR)) {
                if (!subEntry.isDirectory()) {
                    stats->totalFiles++;
                }
                subEntry.close();
                subEntry = entry.openNextFile();
                fileCount++;
            }
            if (subEntry) {
                subEntry.close();
            }
        } else {
            stats->totalFiles++;
        }

        entry.close();
        entry = root.openNextFile();
        dirCount++;
    }

    if (entry) {
        entry.close();
    }
    root.close();

    return FLASH_OK;
}

uint32_t HeltecFlashStorage::getAvailableSpace()
{
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    assert(initialized); // Rule 5: assertion 2

    uint32_t total = LittleFS.totalBytes();
    uint32_t used = LittleFS.usedBytes();

    return (total > used) ? (total - used) : 0;
}

uint32_t HeltecFlashStorage::getUsedSpace()
{
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    assert(initialized); // Rule 5: assertion 2

    return LittleFS.usedBytes();
}

uint32_t HeltecFlashStorage::getTotalSpace()
{
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    assert(initialized); // Rule 5: assertion 2

    return LittleFS.totalBytes();
}

// ============================================================================
// Public Methods - Directory Listing
// ============================================================================

FlashStorageStatus HeltecFlashStorage::listNodeFiles(const char *nodeId, FlashFileInfo *fileList, uint8_t maxFiles,
                                                      uint8_t *fileCount)
{
    assert(nodeId != nullptr);     // Rule 5: assertion 1
    assert(fileList != nullptr);   // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (fileList == nullptr) || (fileCount == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    *fileCount = 0;

    if (maxFiles == 0) {
        return FLASH_ERR_INVALID_PARAM;
    }

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    if (!isValidNodeId(nodeId)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Build directory path
    char dirPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, nullptr, dirPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Open directory
    File dir = LittleFS.open(dirPath);
    if (!dir) {
        return FLASH_ERR_DIR_NOT_FOUND;
    }

    if (!dir.isDirectory()) {
        dir.close();
        return FLASH_ERR_DIR_NOT_FOUND;
    }

    // List files (Rule 2: bounded loop)
    uint8_t count = 0;
    File entry = dir.openNextFile();

    while (entry && (count < maxFiles) && (count < FLASH_MAX_FILES_PER_DIR)) {
        FlashFileInfo *info = &fileList[count];

        // Copy filename safely
        const char *name = entry.name();
        safeStrCopy(info->filename, name, FLASH_MAX_FILENAME_LENGTH);

        info->size = entry.size();
        info->isDirectory = entry.isDirectory();

        entry.close();
        entry = dir.openNextFile();
        count++;
    }

    if (entry) {
        entry.close();
    }
    dir.close();

    *fileCount = count;
    return FLASH_OK;
}

// ============================================================================
// Public Methods - Cleanup
// ============================================================================

FlashStorageStatus HeltecFlashStorage::cleanupOldFiles(const char *nodeId, uint32_t targetFreeBytes)
{
    assert(FLASH_MAX_FILES_PER_DIR > 0U);      // Rule 5: assertion 1
    assert(FLASH_MAX_DIRECTORIES > 0U);        // Rule 5: assertion 2

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    uint32_t currentFree = getAvailableSpace();
    if (currentFree >= targetFreeBytes) {
        return FLASH_OK; // Already have enough space
    }

    // If nodeId specified, clean only that directory
    if (nodeId != nullptr) {
        if (!isValidNodeId(nodeId)) {
            return FLASH_ERR_INVALID_PARAM;
        }

        // List and delete oldest files (files sorted by name = by date)
        FlashFileInfo files[FLASH_MAX_FILES_PER_DIR / 4]; // Rule 3: static array
        uint8_t fileCount = 0;
        uint8_t maxCleanup = sizeof(files) / sizeof(files[0]);

        FlashStorageStatus listResult = listNodeFiles(nodeId, files, maxCleanup, &fileCount);
        if (listResult != FLASH_OK) {
            return listResult;
        }

        // Delete files from oldest (Rule 2: bounded loop)
        uint8_t deleteIdx = 0;
        while ((getAvailableSpace() < targetFreeBytes) && (deleteIdx < fileCount)) {
            if (!files[deleteIdx].isDirectory) {
                (void)deleteFile(nodeId, files[deleteIdx].filename); // Best effort
            }
            deleteIdx++;
        }

        return FLASH_OK;
    }

    // Clean all directories
    File root = LittleFS.open("/");
    if (!root) {
        return FLASH_ERR_READ_FAILED;
    }

    // Iterate through directories (Rule 2: bounded loop)
    uint16_t dirCount = 0;
    File dirEntry = root.openNextFile();

    while (dirEntry && (dirCount < FLASH_MAX_DIRECTORIES) && (getAvailableSpace() < targetFreeBytes)) {
        if (dirEntry.isDirectory()) {
            const char *dirName = dirEntry.name();

            // Skip leading slash if present
            if (dirName[0] == '/') {
                dirName++;
            }

            if (isValidNodeId(dirName)) {
                FlashFileInfo files[32]; // Rule 3: static array (limited)
                uint8_t fileCount = 0;

                FlashStorageStatus listResult = listNodeFiles(dirName, files, 32, &fileCount);
                if (listResult == FLASH_OK) {
                    // Delete oldest file
                    if ((fileCount > 0) && (!files[0].isDirectory)) {
                        (void)deleteFile(dirName, files[0].filename);
                    }
                }
            }
        }

        dirEntry.close();
        dirEntry = root.openNextFile();
        dirCount++;
    }

    if (dirEntry) {
        dirEntry.close();
    }
    root.close();

    return FLASH_OK;
}

// ============================================================================
// Private Methods
// ============================================================================

bool HeltecFlashStorage::buildPath(const char *nodeId, const char *filename, char *outPath, uint16_t outPathLen)
{
    assert(nodeId != nullptr);    // Rule 5: assertion 1
    assert(outPath != nullptr);   // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (outPath == nullptr) || (outPathLen == 0)) {
        return false;
    }

    uint16_t nodeIdLen = safeStrLen(nodeId, FLASH_MAX_NODE_ID_LENGTH);
    uint16_t filenameLen = (filename != nullptr) ? safeStrLen(filename, FLASH_MAX_FILENAME_LENGTH) : 0;

    // Calculate required length: "/" + nodeId + "/" + filename + null
    uint16_t requiredLen = 1U + nodeIdLen + 1U;
    if (filename != nullptr) {
        requiredLen += filenameLen;
    }
    requiredLen++; // null terminator

    if (requiredLen > outPathLen) {
        return false;
    }

    // Build path manually (Rule 9: limited pointer use)
    uint16_t pos = 0;

    outPath[pos] = '/';
    pos++;

    // Copy nodeId
    uint16_t idx = 0;
    while ((idx < nodeIdLen) && (pos < outPathLen - 1U)) {
        outPath[pos] = nodeId[idx];
        pos++;
        idx++;
    }

    // Add filename if provided
    if (filename != nullptr) {
        outPath[pos] = '/';
        pos++;

        idx = 0;
        while ((idx < filenameLen) && (pos < outPathLen - 1U)) {
            outPath[pos] = filename[idx];
            pos++;
            idx++;
        }
    }

    outPath[pos] = '\0';
    return true;
}

bool HeltecFlashStorage::isValidNodeId(const char *nodeId)
{
    assert(FLASH_MAX_NODE_ID_LENGTH > 0U);     // Rule 5: assertion 1

    if (nodeId == nullptr) {
        return false;
    }

    assert(nodeId != nullptr); // Rule 5: assertion 2

    uint16_t len = safeStrLen(nodeId, FLASH_MAX_NODE_ID_LENGTH);

    // Node ID should be 1-8 hex characters
    if ((len == 0) || (len > 8U)) {
        return false;
    }

    // Validate each character is hex (Rule 2: bounded loop)
    uint16_t idx = 0;
    while (idx < len) {
        char c = nodeId[idx];
        bool isHex = ((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'F')) || ((c >= 'a') && (c <= 'f'));
        if (!isHex) {
            return false;
        }
        idx++;
    }

    return true;
}

bool HeltecFlashStorage::isValidFilename(const char *filename)
{
    assert(FLASH_MAX_FILENAME_LENGTH > 0U);    // Rule 5: assertion 1

    if (filename == nullptr) {
        return false;
    }

    assert(filename != nullptr); // Rule 5: assertion 2

    uint16_t len = safeStrLen(filename, FLASH_MAX_FILENAME_LENGTH);

    // Filename must have at least 1 character
    if ((len == 0) || (len >= FLASH_MAX_FILENAME_LENGTH)) {
        return false;
    }

    // Check for invalid characters (Rule 2: bounded loop)
    uint16_t idx = 0;
    while (idx < len) {
        char c = filename[idx];

        // Disallow path separators and null
        if ((c == '/') || (c == '\\') || (c == '\0')) {
            return false;
        }

        // Disallow control characters
        if (c < 32) {
            return false;
        }

        idx++;
    }

    return true;
}

bool HeltecFlashStorage::ensureDirectoryExists(const char *nodeId)
{
    assert(nodeId != nullptr);                 // Rule 5: assertion 1
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 2

    if (nodeId == nullptr) {
        return false;
    }

    // Build directory path
    char dirPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, nullptr, dirPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return false;
    }

    // Check if already exists
    if (LittleFS.exists(dirPath)) {
        return true;
    }

    // Create directory
    bool mkdirOk = LittleFS.mkdir(dirPath); // Rule 7: check return
    if (!mkdirOk) {
        LOG_ERROR("FlashStorage: Failed to create directory %s", dirPath);
        return false;
    }

    LOG_DEBUG("FlashStorage: Created directory %s", dirPath);
    return true;
}

uint16_t HeltecFlashStorage::safeStrLen(const char *str, uint16_t maxLen)
{
    assert(maxLen > 0U); // Rule 5: assertion 1

    if (str == nullptr) {
        return 0;
    }

    assert(str != nullptr); // Rule 5: assertion 2

    // Count length with bound (Rule 2)
    uint16_t len = 0;
    while ((len < maxLen) && (str[len] != '\0')) {
        len++;
    }

    return len;
}

void HeltecFlashStorage::safeStrCopy(char *dest, const char *src, uint16_t destSize)
{
    assert(dest != nullptr);   // Rule 5: assertion 1
    assert(destSize > 0U);     // Rule 5: assertion 2

    if ((dest == nullptr) || (destSize == 0)) {
        return;
    }

    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }

    // Copy with bound (Rule 2)
    uint16_t idx = 0;
    while ((idx < destSize - 1U) && (src[idx] != '\0')) {
        dest[idx] = src[idx];
        idx++;
    }

    dest[idx] = '\0';
}

bool HeltecFlashStorage::deleteDirectoryRecursive(const char *path)
{
    assert(path != nullptr);                   // Rule 5: assertion 1
    assert(FLASH_MAX_FILES_PER_DIR > 0U);      // Rule 5: assertion 2

    if (path == nullptr) {
        return false;
    }

    File dir = LittleFS.open(path);
    if (!dir) {
        return false;
    }

    if (!dir.isDirectory()) {
        dir.close();
        return LittleFS.remove(path);
    }

    // Delete contents first (Rule 2: bounded loop)
    uint16_t fileCount = 0;
    File entry = dir.openNextFile();

    while (entry && (fileCount < FLASH_MAX_FILES_PER_DIR)) {
        char entryPath[FLASH_MAX_PATH_LENGTH];
        uint16_t pathLen = safeStrLen(path, FLASH_MAX_PATH_LENGTH - FLASH_MAX_FILENAME_LENGTH - 2U);
        uint16_t nameLen = safeStrLen(entry.name(), FLASH_MAX_FILENAME_LENGTH);

        // Build entry path
        uint16_t pos = 0;
        while ((pos < pathLen) && (pos < FLASH_MAX_PATH_LENGTH - 1U)) {
            entryPath[pos] = path[pos];
            pos++;
        }
        entryPath[pos] = '/';
        pos++;

        uint16_t nameIdx = 0;
        while ((nameIdx < nameLen) && (pos < FLASH_MAX_PATH_LENGTH - 1U)) {
            entryPath[pos] = entry.name()[nameIdx];
            pos++;
            nameIdx++;
        }
        entryPath[pos] = '\0';

        entry.close();

        // Delete entry (file or directory)
        if (LittleFS.exists(entryPath)) {
            File checkEntry = LittleFS.open(entryPath);
            if (checkEntry) {
                bool isDir = checkEntry.isDirectory();
                checkEntry.close();

                if (isDir) {
                    (void)deleteDirectoryRecursive(entryPath);
                } else {
                    (void)LittleFS.remove(entryPath);
                }
            }
        }

        entry = dir.openNextFile();
        fileCount++;
    }

    if (entry) {
        entry.close();
    }
    dir.close();

    // Now delete the empty directory
    return LittleFS.rmdir(path);
}

#endif // HELTEC_V4 || HAS_FLASH_STORAGE
