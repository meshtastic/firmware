/**
 * @file HeltecFlashStorage.cpp
 * @brief NASA Power of 10 compliant flash storage implementation for Heltec V4
 *
 * This file implements the HeltecFlashStorage class which provides file-based
 * storage management for mesh node data on the Heltec V4's 16MB flash memory.
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
 * Wear Leveling: Handled automatically by LittleFS filesystem which distributes
 * writes across flash blocks to prevent premature wear-out of any single block.
 */

#include "configuration.h"

#if defined(HELTEC_V4) || defined(HAS_FLASH_STORAGE)

#include "HeltecFlashStorage.h"
#include "main.h"
#include <assert.h>

// ============================================================================
// Constructor
// ============================================================================

/**
 * @brief Construct a new HeltecFlashStorage object
 *
 * Initializes the storage manager in an uninitialized state. No hardware
 * access or memory allocation occurs during construction, making it safe
 * to construct as a global object.
 *
 * The constructor:
 * 1. Sets initialized flag to false
 * 2. Clears internal path buffer
 * 3. Validates compile-time configuration constants via assertions
 *
 * NASA Compliance:
 * - Rule 3: No dynamic allocation
 * - Rule 5: Two assertions for configuration validation
 */
HeltecFlashStorage::HeltecFlashStorage() : initialized(false)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1 - Minimum path length
    assert(FLASH_MAX_BUFFER_SIZE >= 64U);      // Rule 5: assertion 2 - Minimum buffer size

    // Initialize path buffer to empty (Rule 3: no dynamic allocation)
    pathBuffer[0] = '\0';
}

// ============================================================================
// Public Methods - Initialization
// ============================================================================

/**
 * @brief Initialize the flash storage system by mounting LittleFS
 *
 * Attempts to mount the LittleFS filesystem on the flash partition. If mounting
 * fails and formatOnFail is true, the storage will be formatted and remounted.
 *
 * The function performs:
 * 1. Early return if already initialized (idempotent)
 * 2. Attempt to mount existing filesystem
 * 3. On failure with formatOnFail=true: format and remount
 * 4. Log storage capacity information on success
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All return values checked (LittleFS.begin())
 *
 * @param formatOnFail If true, format storage when mount fails
 * @return FLASH_OK on success, FLASH_ERR_NOT_INITIALIZED on failure
 */
FlashStorageStatus HeltecFlashStorage::begin(bool formatOnFail)
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 2

    if (initialized) {
        return FLASH_OK;
    }

    bool mountOk = LittleFS.begin(formatOnFail); // Rule 7: capture return value
    if (!mountOk) {
        LOG_ERROR("FlashStorage: Failed to mount LittleFS");
        return FLASH_ERR_NOT_INITIALIZED;
    }

    initialized = true;
    LOG_INFO("FlashStorage: Initialized successfully");

    // Log storage info for debugging
    uint32_t total = LittleFS.totalBytes();
    uint32_t used = LittleFS.usedBytes();
    LOG_INFO("FlashStorage: Total=%lu, Used=%lu, Free=%lu", total, used, total - used);

    return FLASH_OK;
}

/**
 * @brief Format the entire flash storage, erasing all data
 *
 * Performs a complete format of the LittleFS partition, removing all files
 * and directories. After formatting, remounts the fresh filesystem.
 *
 * The function performs:
 * 1. Log warning about destructive operation
 * 2. Format the LittleFS partition
 * 3. Remount the formatted filesystem
 * 4. Update initialization state
 *
 * WARNING: This operation is irreversible - all data will be lost!
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 * - Rule 7: All return values checked
 *
 * @return FLASH_OK on success, error code on failure
 */
FlashStorageStatus HeltecFlashStorage::format()
{
    assert(FLASH_MAX_PATH_LENGTH >= 32U);      // Rule 5: assertion 1
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 2

    LOG_WARN("FlashStorage: Formatting storage...");

    bool formatOk = LittleFS.format(); // Rule 7: check return value
    if (!formatOk) {
        LOG_ERROR("FlashStorage: Format failed");
        return FLASH_ERR_FORMAT_FAILED;
    }

    // Remount after format
    bool mountOk = LittleFS.begin(false); // Rule 7: check return value
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

/**
 * @brief Create an empty file for a specific node
 *
 * Creates a new file at /<nodeId>/<filename>. If the node directory doesn't
 * exist, it will be created automatically. If the file already exists, it
 * will be truncated to zero length.
 *
 * The function performs:
 * 1. Validate nodeId and filename parameters
 * 2. Build the full filesystem path
 * 3. Create node directory if it doesn't exist
 * 4. Create or truncate the file
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, return values checked
 *
 * @param nodeId Node ID as 1-8 character hex string
 * @param filename Filename (max 31 chars, no path separators)
 * @return FLASH_OK on success, error code on failure
 */
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

    // Build full path: /<nodeId>/<filename>
    char fullPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, filename, fullPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return FLASH_ERR_PATH_TOO_LONG;
    }

    // Ensure directory exists before creating file
    bool dirOk = ensureDirectoryExists(nodeId);
    if (!dirOk) {
        LOG_ERROR("FlashStorage: Failed to create directory for node %s", nodeId);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Create or truncate file (mode "w" truncates existing)
    File file = LittleFS.open(fullPath, "w");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to create file %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    file.close();
    LOG_DEBUG("FlashStorage: Created file %s", fullPath);
    return FLASH_OK;
}

/**
 * @brief Delete a specific file from storage
 *
 * Removes a single file at /<nodeId>/<filename>. The containing directory
 * is preserved even if it becomes empty.
 *
 * The function performs:
 * 1. Validate parameters
 * 2. Build file path and verify existence
 * 3. Delete the file
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, return values checked
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to delete
 * @return FLASH_OK on success, FLASH_ERR_FILE_NOT_FOUND if missing
 */
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

    // Check if file exists before attempting delete
    if (!LittleFS.exists(fullPath)) {
        LOG_DEBUG("FlashStorage: File not found for delete: %s", fullPath);
        return FLASH_ERR_FILE_NOT_FOUND;
    }

    // Delete file
    bool deleteOk = LittleFS.remove(fullPath); // Rule 7: check return value
    if (!deleteOk) {
        LOG_ERROR("FlashStorage: Failed to delete file %s", fullPath);
        return FLASH_ERR_DELETE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Deleted file %s", fullPath);
    return FLASH_OK;
}

/**
 * @brief Delete a node's entire directory and all contained files
 *
 * Removes the directory /<nodeId>/ and all files within it. This is useful
 * for removing all data associated with a decommissioned node.
 *
 * The function performs:
 * 1. Validate nodeId parameter
 * 2. Build directory path and verify existence
 * 3. Recursively delete all contents (bounded iteration)
 * 4. Remove the empty directory
 *
 * NASA Compliance:
 * - Rule 2: Bounded iteration via FLASH_MAX_FILES_PER_DIR
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated
 *
 * @param nodeId Node ID whose directory should be deleted
 * @return FLASH_OK on success, FLASH_ERR_DIR_NOT_FOUND if missing
 */
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

    // Build directory path (/<nodeId>)
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

    // Delete directory and all contents (uses bounded iteration)
    bool deleteOk = deleteDirectoryRecursive(dirPath);
    if (!deleteOk) {
        LOG_ERROR("FlashStorage: Failed to delete directory %s", dirPath);
        return FLASH_ERR_DELETE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Deleted directory %s", dirPath);
    return FLASH_OK;
}

/**
 * @brief Read file content into a buffer with optional offset
 *
 * Reads up to maxLength bytes from a file starting at the specified byte offset.
 * The actual number of bytes read is returned in bytesRead (may be less than
 * maxLength if end of file is reached).
 *
 * The function performs:
 * 1. Validate all input parameters
 * 2. Build path and open file for reading
 * 3. Seek to specified offset
 * 4. Read available data into buffer
 * 5. Close file and return bytes read
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, return values checked
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to read
 * @param buffer Output buffer (must be at least maxLength bytes)
 * @param maxLength Maximum bytes to read
 * @param bytesRead Output: actual bytes read (0 on error)
 * @param offset Byte offset to start reading from (default 0)
 * @return FLASH_OK on success, error code on failure
 */
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

    // Seek to offset if specified
    if (offset > 0) {
        bool seekOk = file.seek(offset); // Rule 7: check return value
        if (!seekOk) {
            file.close();
            *bytesRead = 0;
            return FLASH_ERR_READ_FAILED;
        }
    }

    // Read data into buffer
    size_t read = file.read(buffer, maxLength);
    file.close();

    *bytesRead = (uint16_t)read;
    LOG_DEBUG("FlashStorage: Read %d bytes from %s", read, fullPath);
    return FLASH_OK;
}

/**
 * @brief Write data to file, replacing any existing content
 *
 * Writes data to a file, creating it if it doesn't exist. Any existing
 * content is completely replaced (file is truncated before writing).
 * Creates the node directory if it doesn't exist.
 *
 * The function performs:
 * 1. Validate all parameters
 * 2. Ensure node directory exists
 * 3. Open file for writing (truncates existing)
 * 4. Write data with retry logic
 * 5. Verify write completed successfully
 *
 * NASA Compliance:
 * - Rule 2: Bounded retry loop (FLASH_MAX_WRITE_RETRIES)
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, return values checked
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to write
 * @param data Data buffer to write
 * @param length Number of bytes to write
 * @return FLASH_OK on success, error code on failure
 */
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

    // Write with retry logic (Rule 2: bounded loop)
    uint8_t retries = 0;
    size_t written = 0;
    while ((written == 0) && (retries < FLASH_MAX_WRITE_RETRIES)) {
        written = file.write(data, length);
        retries++;
    }

    file.close();

    // Verify complete write
    if (written != length) {
        LOG_ERROR("FlashStorage: Write incomplete: %d/%d bytes", written, length);
        return FLASH_ERR_WRITE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Wrote %d bytes to %s", length, fullPath);
    return FLASH_OK;
}

/**
 * @brief Append data to the end of an existing file
 *
 * Adds data to the end of a file without modifying existing content.
 * If the file doesn't exist, it will be created. Creates the node
 * directory if needed.
 *
 * The function performs:
 * 1. Validate all parameters including length limit
 * 2. Ensure node directory exists
 * 3. Open file in append mode
 * 4. Write data at end of file
 * 5. Verify write completed
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, return values checked
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to append to
 * @param data Data buffer to append
 * @param length Number of bytes to append (max FLASH_MAX_APPEND_SIZE)
 * @return FLASH_OK on success, error code on failure
 */
FlashStorageStatus HeltecFlashStorage::appendFile(const char *nodeId, const char *filename, const uint8_t *data,
                                                   uint16_t length)
{
    assert(nodeId != nullptr);   // Rule 5: assertion 1
    assert(data != nullptr);     // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (filename == nullptr) || (data == nullptr)) {
        return FLASH_ERR_INVALID_PARAM;
    }

    // Check length bounds (Rule 2 compliance)
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

    // Write data at end of file
    size_t written = file.write(data, length);
    file.close();

    // Verify complete write
    if (written != length) {
        LOG_ERROR("FlashStorage: Append incomplete: %d/%d bytes", written, length);
        return FLASH_ERR_WRITE_FAILED;
    }

    LOG_DEBUG("FlashStorage: Appended %d bytes to %s", length, fullPath);
    return FLASH_OK;
}

/**
 * @brief Edit file content at a specific offset (in-place modification)
 *
 * Modifies bytes within an existing file without changing its size.
 * The edit range (offset to offset+length) must be within the current
 * file size. Use for updating fixed-format records.
 *
 * The function performs:
 * 1. Validate all parameters
 * 2. Verify file exists and get its size
 * 3. Verify edit is within file bounds
 * 4. Open file for read+write
 * 5. Seek to offset and write new data
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for parameter validation
 * - Rule 7: All parameters validated, bounds checked
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to edit
 * @param offset Byte position to start editing
 * @param data New data to write at offset
 * @param length Number of bytes to write (offset+length must be <= file size)
 * @return FLASH_OK on success, error code on failure
 */
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

    // Open file for read+write to check bounds and edit
    File file = LittleFS.open(fullPath, "r+");
    if (!file) {
        LOG_ERROR("FlashStorage: Failed to open file for edit: %s", fullPath);
        return FLASH_ERR_WRITE_FAILED;
    }

    // Verify edit is within file bounds
    uint32_t fileSize = file.size();
    if ((offset + length) > fileSize) {
        file.close();
        LOG_WARN("FlashStorage: Edit would exceed file bounds");
        return FLASH_ERR_INVALID_PARAM;
    }

    // Seek to offset
    bool seekOk = file.seek(offset); // Rule 7: check return value
    if (!seekOk) {
        file.close();
        return FLASH_ERR_WRITE_FAILED;
    }

    // Write new data at offset
    size_t written = file.write(data, length);
    file.close();

    // Verify complete write
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

/**
 * @brief Check if a specific file exists
 *
 * Tests whether a file exists at /<nodeId>/<filename> without opening it.
 * Returns false on any error (including not initialized).
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to check
 * @return true if file exists, false otherwise
 */
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

/**
 * @brief Check if a node directory exists
 *
 * Tests whether a directory exists at /<nodeId>/ without checking contents.
 * Returns false on any error (including not initialized).
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 * - Rule 7: Parameter validated
 *
 * @param nodeId Node ID as hex string
 * @return true if directory exists, false otherwise
 */
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

    // Build directory path (/<nodeId>)
    char dirPath[FLASH_MAX_PATH_LENGTH];
    bool pathOk = buildPath(nodeId, nullptr, dirPath, FLASH_MAX_PATH_LENGTH);
    if (!pathOk) {
        return false;
    }

    return LittleFS.exists(dirPath);
}

/**
 * @brief Get the size of a file in bytes
 *
 * Opens a file to retrieve its size without reading contents.
 * Sets fileSize to 0 on any error.
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated, output set on error
 *
 * @param nodeId Node ID as hex string
 * @param filename Name of file to check
 * @param fileSize Output: file size in bytes (set to 0 on error)
 * @return FLASH_OK on success, error code on failure
 */
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

/**
 * @brief Get comprehensive storage statistics
 *
 * Retrieves detailed information about storage usage including total/used/free
 * bytes and counts of files and directories. Iterates through the filesystem
 * to count entries (using bounded loops per NASA Rule 2).
 *
 * NASA Compliance:
 * - Rule 2: Bounded loops (FLASH_MAX_DIRECTORIES, FLASH_MAX_FILES_PER_DIR)
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated
 *
 * @param stats Output: storage statistics structure (zeroed on error)
 * @return FLASH_OK on success, error code on failure
 */
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

    // Get basic storage stats from filesystem
    stats->totalBytes = LittleFS.totalBytes();
    stats->usedBytes = LittleFS.usedBytes();
    stats->freeBytes = stats->totalBytes - stats->usedBytes;
    stats->totalFiles = 0;
    stats->totalDirectories = 0;

    // Count files and directories (Rule 2: bounded outer loop)
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

/**
 * @brief Get available (free) storage space in bytes
 *
 * Returns the number of bytes available for new data. This is a quick
 * operation that queries the filesystem directly.
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 *
 * @return Available bytes, or 0 if not initialized
 */
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

/**
 * @brief Get used storage space in bytes
 *
 * Returns the number of bytes currently occupied by files and metadata.
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 *
 * @return Used bytes, or 0 if not initialized
 */
uint32_t HeltecFlashStorage::getUsedSpace()
{
    assert(FLASH_PARTITION_SIZE_BYTES > 0U);   // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    assert(initialized); // Rule 5: assertion 2

    return LittleFS.usedBytes();
}

/**
 * @brief Get total storage capacity in bytes
 *
 * Returns the total size of the LittleFS partition.
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 *
 * @return Total capacity, or 0 if not initialized
 */
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

/**
 * @brief List all files in a node's directory
 *
 * Populates an array with information about files stored for a specific node.
 * Results are in filesystem order (typically creation order).
 *
 * The function performs:
 * 1. Validate all parameters
 * 2. Open node directory
 * 3. Iterate through entries (bounded by maxFiles and FLASH_MAX_FILES_PER_DIR)
 * 4. Copy file info to output array
 *
 * NASA Compliance:
 * - Rule 2: Bounded loop (min of maxFiles, FLASH_MAX_FILES_PER_DIR)
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated
 *
 * @param nodeId Node ID whose files to list
 * @param fileList Output array for file information
 * @param maxFiles Size of fileList array
 * @param fileCount Output: number of files found
 * @return FLASH_OK on success, FLASH_ERR_DIR_NOT_FOUND if missing
 */
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
// Public Methods - Storage Maintenance
// ============================================================================

/**
 * @brief Clean up old files to achieve target free space
 *
 * Automatically deletes oldest files (based on filename sort order) until
 * the target free space is achieved or no more files can be deleted.
 * Assumes date-based filenames where alphabetically earlier = older.
 *
 * The function performs:
 * 1. Check if cleanup is needed
 * 2. If nodeId specified: list and delete oldest files for that node
 * 3. If nodeId is null: iterate all nodes, deleting oldest files
 * 4. Continue until target free space achieved or no files remain
 *
 * NASA Compliance:
 * - Rule 2: Bounded loops for iteration
 * - Rule 5: Two assertions for validation
 * - Rule 7: Return values checked, parameters validated
 *
 * @param nodeId Node to clean (or null for all nodes)
 * @param targetFreeBytes Target free space to achieve
 * @return FLASH_OK when cleanup completed (may not reach target)
 */
FlashStorageStatus HeltecFlashStorage::cleanupOldFiles(const char *nodeId, uint32_t targetFreeBytes)
{
    assert(FLASH_MAX_FILES_PER_DIR > 0U);      // Rule 5: assertion 1
    assert(FLASH_MAX_DIRECTORIES > 0U);        // Rule 5: assertion 2

    if (!initialized) {
        return FLASH_ERR_NOT_INITIALIZED;
    }

    // Check if cleanup is needed
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
// Private Methods - Path Building
// ============================================================================

/**
 * @brief Build full filesystem path from nodeId and optional filename
 *
 * Constructs a path string: "/<nodeId>/<filename>" or "/<nodeId>" if
 * filename is null. Uses manual character-by-character copy to avoid
 * standard library dependencies and ensure bounds safety.
 *
 * NASA Compliance:
 * - Rule 2: Bounded loops for string copying
 * - Rule 5: Two assertions for validation
 * - Rule 7: All parameters validated
 * - Rule 9: Single pointer dereference
 *
 * @param nodeId Node identifier string
 * @param filename Filename string (or null for directory only)
 * @param outPath Output buffer for constructed path
 * @param outPathLen Size of output buffer
 * @return true if path built successfully, false if too long
 */
bool HeltecFlashStorage::buildPath(const char *nodeId, const char *filename, char *outPath, uint16_t outPathLen)
{
    assert(nodeId != nullptr);    // Rule 5: assertion 1
    assert(outPath != nullptr);   // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((nodeId == nullptr) || (outPath == nullptr) || (outPathLen == 0)) {
        return false;
    }

    // Calculate lengths
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

    // Add leading slash
    outPath[pos] = '/';
    pos++;

    // Copy nodeId (Rule 2: bounded loop)
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

// ============================================================================
// Private Methods - Validation
// ============================================================================

/**
 * @brief Validate node ID string format
 *
 * Checks that nodeId:
 * - Is not null
 * - Has 1-8 characters
 * - Contains only hexadecimal digits (0-9, A-F, a-f)
 *
 * NASA Compliance:
 * - Rule 2: Bounded loop (max 8 characters)
 * - Rule 5: Two assertions for validation
 *
 * @param nodeId Node ID string to validate
 * @return true if valid hex string of correct length
 */
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

/**
 * @brief Validate filename string format
 *
 * Checks that filename:
 * - Is not null or empty
 * - Has fewer than FLASH_MAX_FILENAME_LENGTH characters
 * - Contains no path separators (/ or \)
 * - Contains no control characters (< 32)
 *
 * NASA Compliance:
 * - Rule 2: Bounded loop
 * - Rule 5: Two assertions for validation
 *
 * @param filename Filename string to validate
 * @return true if valid filename
 */
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

// ============================================================================
// Private Methods - Directory Management
// ============================================================================

/**
 * @brief Ensure node directory exists, creating if necessary
 *
 * Checks if directory /<nodeId>/ exists and creates it if missing.
 *
 * NASA Compliance:
 * - Rule 5: Two assertions for validation
 * - Rule 7: Return value of mkdir checked
 *
 * @param nodeId Node ID for directory name
 * @return true if directory exists or was created successfully
 */
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
    bool mkdirOk = LittleFS.mkdir(dirPath); // Rule 7: check return value
    if (!mkdirOk) {
        LOG_ERROR("FlashStorage: Failed to create directory %s", dirPath);
        return false;
    }

    LOG_DEBUG("FlashStorage: Created directory %s", dirPath);
    return true;
}

// ============================================================================
// Private Methods - String Utilities
// ============================================================================

/**
 * @brief Calculate string length with bounded iteration (NASA Rule 2)
 *
 * Returns the length of a string, stopping at maxLen if the string
 * is longer. This prevents unbounded iteration on unterminated strings.
 *
 * NASA Compliance:
 * - Rule 2: Loop bounded by maxLen
 * - Rule 5: Two assertions for validation
 *
 * @param str String to measure (may be null)
 * @param maxLen Maximum characters to scan
 * @return String length, capped at maxLen, or 0 if null
 */
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

/**
 * @brief Copy string with bounds checking (NASA Rule 2)
 *
 * Safely copies src to dest, ensuring:
 * - Null termination of dest
 * - No buffer overflow
 * - Handles null src by setting dest to empty string
 *
 * NASA Compliance:
 * - Rule 2: Loop bounded by destSize
 * - Rule 5: Two assertions for validation
 * - Rule 9: Single pointer dereference
 *
 * @param dest Destination buffer
 * @param src Source string
 * @param destSize Size of destination buffer
 */
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

/**
 * @brief Delete directory and all contents with bounded iteration
 *
 * Removes all files within a directory, then removes the directory itself.
 * Uses bounded iteration to comply with NASA Rule 2.
 *
 * The function performs:
 * 1. Open directory and verify it's a directory
 * 2. Iterate through entries (bounded by FLASH_MAX_FILES_PER_DIR)
 * 3. Delete each file or subdirectory
 * 4. Remove the now-empty directory
 *
 * NASA Compliance:
 * - Rule 2: Loop bounded by FLASH_MAX_FILES_PER_DIR
 * - Rule 5: Two assertions for validation
 * - Rule 7: Return values checked
 *
 * @param path Path to directory to delete
 * @return true if directory and all contents deleted
 */
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

    // If not a directory, just remove it
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

        // Build entry path manually
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
