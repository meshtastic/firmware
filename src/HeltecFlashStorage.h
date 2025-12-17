#pragma once

#include "configuration.h"

#if defined(HELTEC_V4) || defined(HAS_FLASH_STORAGE)

#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <LittleFS.h>
#include <assert.h>

/**
 * @file HeltecFlashStorage.h
 * @brief NASA Power of 10 compliant flash storage manager for Heltec V4 (16MB)
 *
 * This class provides file-based storage management for mesh node data with:
 * - Directory structure: /<nodeId>/<date>.dat
 * - Wear leveling via LittleFS (built-in)
 * - No dynamic memory allocation after initialization
 * - Thread-safe file operations
 *
 * NASA Power of 10 Compliance:
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
 */

// ============================================================================
// Configuration Constants
// ============================================================================

// Flash storage configuration (16MB flash)
#ifndef FLASH_STORAGE_SIZE_BYTES
#define FLASH_STORAGE_SIZE_BYTES (16U * 1024U * 1024U) // 16MB
#endif

// Partition size for LittleFS (leave space for firmware)
#ifndef FLASH_PARTITION_SIZE_BYTES
#define FLASH_PARTITION_SIZE_BYTES (8U * 1024U * 1024U) // 8MB for data
#endif

// Maximum path length for files (Rule 2: fixed bound)
#ifndef FLASH_MAX_PATH_LENGTH
#define FLASH_MAX_PATH_LENGTH 64U
#endif

// Maximum file size for single read/write operation (Rule 3: static buffer)
#ifndef FLASH_MAX_BUFFER_SIZE
#define FLASH_MAX_BUFFER_SIZE 512U
#endif

// Maximum node ID string length (8 hex chars + null)
#define FLASH_MAX_NODE_ID_LENGTH 9U

// Maximum date string length (YYYY-MM-DD + null)
#define FLASH_MAX_DATE_LENGTH 11U

// Maximum filename length
#define FLASH_MAX_FILENAME_LENGTH 32U

// Maximum number of files per directory (Rule 2: loop bound)
#define FLASH_MAX_FILES_PER_DIR 256U

// Maximum number of directories (Rule 2: loop bound)
#define FLASH_MAX_DIRECTORIES 128U

// Maximum write retries (Rule 2: loop bound)
#define FLASH_MAX_WRITE_RETRIES 3U

// Maximum bytes to append in single operation
#define FLASH_MAX_APPEND_SIZE 256U

// ============================================================================
// Status Codes
// ============================================================================

/**
 * @brief Storage operation status codes
 *
 * These codes indicate the result of storage operations.
 * FLASH_OK (0) indicates success, all other values indicate errors.
 */
enum FlashStorageStatus {
    FLASH_OK = 0,                    ///< Operation completed successfully
    FLASH_ERR_NOT_INITIALIZED = 1,   ///< Storage system not initialized (call begin() first)
    FLASH_ERR_INVALID_PARAM = 2,     ///< Invalid parameter (null pointer, empty string, etc.)
    FLASH_ERR_FILE_NOT_FOUND = 3,    ///< Requested file does not exist
    FLASH_ERR_DIR_NOT_FOUND = 4,     ///< Requested directory does not exist
    FLASH_ERR_STORAGE_FULL = 5,      ///< No space left on storage device
    FLASH_ERR_WRITE_FAILED = 6,      ///< Write operation failed (hardware or filesystem error)
    FLASH_ERR_READ_FAILED = 7,       ///< Read operation failed (hardware or filesystem error)
    FLASH_ERR_DELETE_FAILED = 8,     ///< Delete operation failed (file locked or hardware error)
    FLASH_ERR_PATH_TOO_LONG = 9,     ///< Constructed path exceeds FLASH_MAX_PATH_LENGTH
    FLASH_ERR_BUFFER_TOO_SMALL = 10, ///< Provided buffer too small for requested data
    FLASH_ERR_FORMAT_FAILED = 11     ///< Filesystem format operation failed
};

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief File information structure (no dynamic allocation)
 *
 * Contains metadata about a single file or directory entry.
 * Used by listNodeFiles() to return directory contents.
 *
 * @note Filename is stored in a fixed-size array to avoid dynamic allocation
 *       per NASA Power of 10 Rule 3.
 */
struct FlashFileInfo {
    char filename[FLASH_MAX_FILENAME_LENGTH]; ///< Filename without path (null-terminated)
    uint32_t size;                            ///< File size in bytes (0 for directories)
    bool isDirectory;                         ///< True if entry is a subdirectory
};

/**
 * @brief Storage statistics structure
 *
 * Contains aggregate information about flash storage usage.
 * Populated by getStorageStats().
 */
struct FlashStorageStats {
    uint32_t totalBytes;      ///< Total storage capacity in bytes
    uint32_t usedBytes;       ///< Bytes currently in use
    uint32_t freeBytes;       ///< Bytes available for new data
    uint16_t totalFiles;      ///< Number of files across all directories
    uint16_t totalDirectories; ///< Number of node directories
};

// ============================================================================
// Main Storage Class
// ============================================================================

/**
 * @brief NASA Power of 10 compliant flash storage class for Heltec V4
 *
 * Provides file-based storage with wear leveling for mesh node data.
 * Organizes data in a directory structure: /<nodeId>/<filename>
 *
 * Example directory structure:
 * ```
 * /AABBCCDD/              <- Node directory (8-char hex ID)
 *   2024-01-15.dat        <- Daily data file
 *   2024-01-16.dat
 * /11223344/              <- Another node
 *   2024-01-15.dat
 * ```
 *
 * Thread Safety:
 * - All public methods are safe to call from any thread
 * - Uses LittleFS which handles internal locking
 *
 * Memory Usage:
 * - No heap allocation after construction
 * - Uses fixed-size buffers for paths and temporary data
 *
 * Wear Leveling:
 * - Handled automatically by LittleFS filesystem
 * - Distributes writes across flash blocks evenly
 */
class HeltecFlashStorage
{
  public:
    // ========================================================================
    // Constructor
    // ========================================================================

    /**
     * @brief Construct a new HeltecFlashStorage object
     *
     * Initializes internal state without allocating memory or accessing hardware.
     * The storage system is not usable until begin() is called.
     *
     * Memory: Uses only stack and static member variables (no heap allocation).
     *
     * Post-conditions:
     * - isInitialized() returns false
     * - All internal buffers are zeroed
     *
     * Example:
     * ```cpp
     * HeltecFlashStorage storage;  // Safe to construct globally
     * ```
     */
    HeltecFlashStorage();

    // ========================================================================
    // Initialization Methods
    // ========================================================================

    /**
     * @brief Initialize the flash storage system
     *
     * Mounts the LittleFS filesystem on the flash partition. Must be called
     * before any other storage operations. Safe to call multiple times
     * (subsequent calls return FLASH_OK immediately if already initialized).
     *
     * This function performs:
     * 1. Attempts to mount existing LittleFS filesystem
     * 2. If mount fails and formatOnFail is true, formats and remounts
     * 3. Logs storage capacity information
     *
     * Pre-conditions:
     * - Flash hardware must be accessible
     * - Partition table must include LittleFS partition
     *
     * Post-conditions (on success):
     * - isInitialized() returns true
     * - All file operations become available
     *
     * @param formatOnFail If true, automatically format storage if mount fails.
     *                     Set to false to detect unformatted/corrupted storage.
     *                     Default: true
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Initialization successful
     *         - FLASH_ERR_NOT_INITIALIZED: Mount failed (and format disabled or failed)
     *
     * Example:
     * ```cpp
     * HeltecFlashStorage storage;
     * FlashStorageStatus status = storage.begin(true);
     * if (status != FLASH_OK) {
     *     // Handle initialization failure
     * }
     * ```
     */
    FlashStorageStatus begin(bool formatOnFail = true);

    /**
     * @brief Format the entire flash storage (erase all data)
     *
     * Completely erases all files and directories, returning the storage
     * to a clean state. Use with caution - all data will be permanently lost.
     *
     * This function performs:
     * 1. Formats the LittleFS partition (erases all data)
     * 2. Remounts the fresh filesystem
     * 3. Updates initialization state
     *
     * Pre-conditions:
     * - None (can be called before or after begin())
     *
     * Post-conditions (on success):
     * - All files and directories are deleted
     * - Storage is empty and ready for use
     * - isInitialized() returns true
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Format and remount successful
     *         - FLASH_ERR_FORMAT_FAILED: Format operation failed
     *         - FLASH_ERR_NOT_INITIALIZED: Remount after format failed
     *
     * @warning This operation is irreversible. All data will be lost.
     *
     * Example:
     * ```cpp
     * // Factory reset scenario
     * FlashStorageStatus status = storage.format();
     * if (status == FLASH_OK) {
     *     LOG_INFO("Storage formatted successfully");
     * }
     * ```
     */
    FlashStorageStatus format();

    // ========================================================================
    // File Creation and Deletion Methods
    // ========================================================================

    /**
     * @brief Create a new empty file for a specific node
     *
     * Creates a file at /<nodeId>/<filename>. If the node directory doesn't
     * exist, it will be created automatically. If the file already exists,
     * it will be truncated to zero length.
     *
     * This function performs:
     * 1. Validates nodeId and filename parameters
     * 2. Creates node directory if it doesn't exist
     * 3. Creates or truncates the file
     *
     * Pre-conditions:
     * - Storage must be initialized (begin() called successfully)
     * - nodeId must be 1-8 hexadecimal characters
     * - filename must be 1-31 characters, no path separators
     *
     * Post-conditions (on success):
     * - File exists at /<nodeId>/<filename>
     * - File size is 0 bytes
     * - Node directory exists
     *
     * @param nodeId Node identifier as hex string (e.g., "AABBCCDD").
     *               Max 8 characters. Must not be null.
     * @param filename Name of file to create (e.g., "2024-01-15.dat").
     *                 Max 31 characters. Must not be null.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: File created successfully
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid nodeId or filename
     *         - FLASH_ERR_PATH_TOO_LONG: Combined path exceeds limit
     *         - FLASH_ERR_WRITE_FAILED: Could not create directory or file
     *
     * Example:
     * ```cpp
     * FlashStorageStatus status = storage.createFile("AABBCCDD", "2024-01-15.dat");
     * if (status == FLASH_OK) {
     *     // File ready for writing
     * }
     * ```
     */
    FlashStorageStatus createFile(const char *nodeId, const char *filename);

    /**
     * @brief Delete a specific file
     *
     * Removes a single file from storage. The containing directory is
     * not removed even if empty.
     *
     * This function performs:
     * 1. Validates parameters
     * 2. Checks file existence
     * 3. Deletes the file
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - File must exist
     *
     * Post-conditions (on success):
     * - File no longer exists
     * - Directory structure unchanged
     * - Storage space freed
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to delete. Must not be null.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: File deleted successfully
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid nodeId or filename
     *         - FLASH_ERR_FILE_NOT_FOUND: File does not exist
     *         - FLASH_ERR_DELETE_FAILED: Could not delete file
     *
     * Example:
     * ```cpp
     * // Delete old data file
     * storage.deleteFile("AABBCCDD", "2024-01-01.dat");
     * ```
     */
    FlashStorageStatus deleteFile(const char *nodeId, const char *filename);

    /**
     * @brief Delete all files for a specific node (entire directory)
     *
     * Removes a node's directory and all files within it. Use to completely
     * remove a node's data from storage.
     *
     * This function performs:
     * 1. Validates nodeId parameter
     * 2. Deletes all files in the directory (bounded iteration)
     * 3. Removes the empty directory
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - Directory must exist
     *
     * Post-conditions (on success):
     * - Directory /<nodeId>/ no longer exists
     * - All files within are deleted
     * - Storage space freed
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Directory and contents deleted
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid nodeId
     *         - FLASH_ERR_DIR_NOT_FOUND: Directory does not exist
     *         - FLASH_ERR_DELETE_FAILED: Could not delete directory/files
     *
     * @note Uses bounded iteration (max FLASH_MAX_FILES_PER_DIR) per NASA Rule 2
     *
     * Example:
     * ```cpp
     * // Remove all data for a decommissioned node
     * storage.deleteNodeDirectory("AABBCCDD");
     * ```
     */
    FlashStorageStatus deleteNodeDirectory(const char *nodeId);

    // ========================================================================
    // File Read/Write Methods
    // ========================================================================

    /**
     * @brief Read file content into a buffer
     *
     * Reads up to maxLength bytes from a file starting at the specified offset.
     * Partial reads are supported - the actual number of bytes read is returned
     * in bytesRead.
     *
     * This function performs:
     * 1. Validates all parameters
     * 2. Opens file for reading
     * 3. Seeks to offset position
     * 4. Reads available data into buffer
     * 5. Closes file and returns bytes read
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - File must exist
     * - buffer and bytesRead must not be null
     * - maxLength must be > 0
     *
     * Post-conditions (on success):
     * - buffer contains up to maxLength bytes of file data
     * - bytesRead contains actual bytes read (may be less than maxLength)
     * - File is closed
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to read. Must not be null.
     * @param buffer Output buffer to store read data. Must not be null.
     *               Must be at least maxLength bytes.
     * @param maxLength Maximum number of bytes to read. Must be > 0.
     * @param bytesRead Output: actual number of bytes read. Must not be null.
     *                  Set to 0 on error.
     * @param offset Byte offset in file to start reading from. Default: 0.
     *               If offset > file size, bytesRead will be 0.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Read successful (check bytesRead for amount)
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters
     *         - FLASH_ERR_FILE_NOT_FOUND: File does not exist
     *         - FLASH_ERR_READ_FAILED: Seek or read operation failed
     *         - FLASH_ERR_PATH_TOO_LONG: Path exceeds limit
     *
     * Example:
     * ```cpp
     * uint8_t buffer[256];
     * uint16_t bytesRead;
     * FlashStorageStatus status = storage.readFile(
     *     "AABBCCDD", "2024-01-15.dat",
     *     buffer, sizeof(buffer), &bytesRead, 0);
     * if (status == FLASH_OK) {
     *     // Process bytesRead bytes from buffer
     * }
     * ```
     */
    FlashStorageStatus readFile(const char *nodeId, const char *filename, uint8_t *buffer, uint16_t maxLength,
                                uint16_t *bytesRead, uint32_t offset = 0);

    /**
     * @brief Write data to file (overwrites existing content)
     *
     * Writes data to a file, replacing any existing content. If the file
     * doesn't exist, it will be created. If the node directory doesn't
     * exist, it will be created.
     *
     * This function performs:
     * 1. Validates all parameters
     * 2. Creates node directory if needed
     * 3. Opens file for writing (truncates existing)
     * 4. Writes data with retry logic
     * 5. Closes file
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - data must not be null
     * - length must be > 0
     *
     * Post-conditions (on success):
     * - File exists with exactly 'length' bytes
     * - File contains copy of 'data'
     * - Previous content is lost
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to write. Must not be null.
     * @param data Data buffer to write. Must not be null.
     * @param length Number of bytes to write. Must be > 0.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Write successful
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters
     *         - FLASH_ERR_PATH_TOO_LONG: Path exceeds limit
     *         - FLASH_ERR_WRITE_FAILED: Write operation failed
     *
     * @note Retries up to FLASH_MAX_WRITE_RETRIES times on failure
     *
     * Example:
     * ```cpp
     * const char *data = "sensor_data:123";
     * FlashStorageStatus status = storage.writeFile(
     *     "AABBCCDD", "2024-01-15.dat",
     *     (const uint8_t*)data, strlen(data));
     * ```
     */
    FlashStorageStatus writeFile(const char *nodeId, const char *filename, const uint8_t *data, uint16_t length);

    /**
     * @brief Append data to end of existing file
     *
     * Adds data to the end of a file without modifying existing content.
     * If the file doesn't exist, it will be created. Ideal for log files
     * or accumulating sensor data throughout the day.
     *
     * This function performs:
     * 1. Validates all parameters
     * 2. Creates node directory if needed
     * 3. Opens file in append mode
     * 4. Writes data at end of file
     * 5. Closes file
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - data must not be null
     * - length must be > 0 and <= FLASH_MAX_APPEND_SIZE
     *
     * Post-conditions (on success):
     * - File size increased by 'length' bytes
     * - New data is at end of file
     * - Existing content unchanged
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to append to. Must not be null.
     * @param data Data buffer to append. Must not be null.
     * @param length Number of bytes to append. Must be 1-FLASH_MAX_APPEND_SIZE.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Append successful
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters or length > max
     *         - FLASH_ERR_PATH_TOO_LONG: Path exceeds limit
     *         - FLASH_ERR_WRITE_FAILED: Append operation failed
     *
     * Example:
     * ```cpp
     * // Add new sensor reading to daily log
     * char entry[64];
     * snprintf(entry, sizeof(entry), "12:30:00,temp=23.5\n");
     * storage.appendFile("AABBCCDD", "2024-01-15.dat",
     *                    (const uint8_t*)entry, strlen(entry));
     * ```
     */
    FlashStorageStatus appendFile(const char *nodeId, const char *filename, const uint8_t *data, uint16_t length);

    /**
     * @brief Edit file content at a specific offset (in-place modification)
     *
     * Modifies existing file content without changing file size. The edit
     * must fit within the existing file bounds (offset + length <= file size).
     * Use for updating fixed-format records or correcting data.
     *
     * This function performs:
     * 1. Validates all parameters
     * 2. Verifies file exists and edit is within bounds
     * 3. Opens file for read+write
     * 4. Seeks to offset position
     * 5. Writes new data
     * 6. Closes file
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - File must exist
     * - offset + length must be <= file size
     *
     * Post-conditions (on success):
     * - Bytes at [offset, offset+length) replaced with new data
     * - File size unchanged
     * - Data outside edit range unchanged
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to edit. Must not be null.
     * @param offset Byte position where edit starts. Must be within file.
     * @param data New data to write at offset. Must not be null.
     * @param length Number of bytes to write. offset+length must be <= file size.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Edit successful
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters or edit out of bounds
     *         - FLASH_ERR_FILE_NOT_FOUND: File does not exist
     *         - FLASH_ERR_WRITE_FAILED: Write operation failed
     *
     * Example:
     * ```cpp
     * // Update status byte at offset 10
     * uint8_t newStatus = 0x02;
     * storage.editFile("AABBCCDD", "2024-01-15.dat", 10, &newStatus, 1);
     * ```
     */
    FlashStorageStatus editFile(const char *nodeId, const char *filename, uint32_t offset, const uint8_t *data,
                                uint16_t length);

    // ========================================================================
    // File Information Methods
    // ========================================================================

    /**
     * @brief Check if a specific file exists
     *
     * Tests whether a file exists at the specified node/filename path.
     * Does not open or read the file.
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - Parameters must be valid
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to check. Must not be null.
     *
     * @return true if file exists, false if not found or on any error
     *
     * Example:
     * ```cpp
     * if (storage.fileExists("AABBCCDD", "2024-01-15.dat")) {
     *     // File exists, safe to read
     * }
     * ```
     */
    bool fileExists(const char *nodeId, const char *filename);

    /**
     * @brief Check if a node directory exists
     *
     * Tests whether a directory exists for the specified node ID.
     * Does not check for files within the directory.
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - nodeId must be valid
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     *
     * @return true if directory exists, false if not found or on any error
     *
     * Example:
     * ```cpp
     * if (!storage.nodeDirectoryExists("AABBCCDD")) {
     *     // First time seeing this node
     * }
     * ```
     */
    bool nodeDirectoryExists(const char *nodeId);

    /**
     * @brief Get the size of a file in bytes
     *
     * Retrieves the current size of a file without reading its contents.
     * Useful for allocating buffers before reading.
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - File must exist
     * - fileSize must not be null
     *
     * Post-conditions (on success):
     * - fileSize contains the file size in bytes
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param filename Name of file to check. Must not be null.
     * @param fileSize Output: file size in bytes. Must not be null.
     *                 Set to 0 on error.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Size retrieved successfully
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters
     *         - FLASH_ERR_FILE_NOT_FOUND: File does not exist
     *
     * Example:
     * ```cpp
     * uint32_t size;
     * if (storage.getFileSize("AABBCCDD", "2024-01-15.dat", &size) == FLASH_OK) {
     *     // Allocate buffer or decide on read strategy
     * }
     * ```
     */
    FlashStorageStatus getFileSize(const char *nodeId, const char *filename, uint32_t *fileSize);

    // ========================================================================
    // Storage Statistics Methods
    // ========================================================================

    /**
     * @brief Get comprehensive storage statistics
     *
     * Retrieves detailed information about storage usage including:
     * - Total/used/free byte counts
     * - Number of files and directories
     *
     * Iterates through all directories and files (with bounded loops).
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - stats must not be null
     *
     * Post-conditions (on success):
     * - stats structure fully populated
     *
     * @param stats Output: storage statistics structure. Must not be null.
     *              All fields set to 0 on error.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Statistics retrieved successfully
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: stats is null
     *
     * @note File/directory counts use bounded iteration per NASA Rule 2
     *
     * Example:
     * ```cpp
     * FlashStorageStats stats;
     * if (storage.getStorageStats(&stats) == FLASH_OK) {
     *     LOG_INFO("Storage: %lu/%lu bytes used, %d files",
     *              stats.usedBytes, stats.totalBytes, stats.totalFiles);
     * }
     * ```
     */
    FlashStorageStatus getStorageStats(FlashStorageStats *stats);

    /**
     * @brief Get available (free) storage space in bytes
     *
     * Returns the number of bytes available for new files.
     * Quick operation - does not iterate files.
     *
     * Pre-conditions:
     * - Storage should be initialized (returns 0 if not)
     *
     * @return Available bytes, or 0 if not initialized
     *
     * Example:
     * ```cpp
     * uint32_t freeSpace = storage.getAvailableSpace();
     * if (freeSpace < 10000) {
     *     // Trigger cleanup
     * }
     * ```
     */
    uint32_t getAvailableSpace();

    /**
     * @brief Get used storage space in bytes
     *
     * Returns the number of bytes currently occupied by files and metadata.
     * Quick operation - does not iterate files.
     *
     * Pre-conditions:
     * - Storage should be initialized (returns 0 if not)
     *
     * @return Used bytes, or 0 if not initialized
     */
    uint32_t getUsedSpace();

    /**
     * @brief Get total storage capacity in bytes
     *
     * Returns the total size of the flash partition.
     * Quick operation - does not iterate files.
     *
     * Pre-conditions:
     * - Storage should be initialized (returns 0 if not)
     *
     * @return Total capacity in bytes, or 0 if not initialized
     */
    uint32_t getTotalSpace();

    /**
     * @brief Check if storage system is initialized and ready
     *
     * Returns the initialization state without accessing hardware.
     * Always safe to call.
     *
     * @return true if begin() completed successfully, false otherwise
     */
    bool isInitialized() const { return initialized; }

    // ========================================================================
    // Directory Listing Methods
    // ========================================================================

    /**
     * @brief List all files in a node's directory
     *
     * Retrieves information about files stored for a specific node.
     * Results are returned in filesystem order (typically creation order).
     *
     * This function performs:
     * 1. Validates parameters
     * 2. Opens node directory
     * 3. Iterates through entries (bounded)
     * 4. Populates fileList array
     * 5. Returns count of files found
     *
     * Pre-conditions:
     * - Storage must be initialized
     * - Directory must exist
     * - fileList array must have space for maxFiles entries
     *
     * Post-conditions (on success):
     * - fileList[0..fileCount-1] populated with file info
     * - fileCount contains number of entries found
     *
     * @param nodeId Node identifier as hex string. Must not be null.
     * @param fileList Output array of FlashFileInfo structures. Must not be null.
     * @param maxFiles Maximum entries to return (size of fileList array).
     * @param fileCount Output: actual number of files found. Must not be null.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Listing successful (even if 0 files)
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid parameters
     *         - FLASH_ERR_DIR_NOT_FOUND: Node directory does not exist
     *
     * @note Iteration bounded by min(maxFiles, FLASH_MAX_FILES_PER_DIR)
     *
     * Example:
     * ```cpp
     * FlashFileInfo files[32];
     * uint8_t count;
     * if (storage.listNodeFiles("AABBCCDD", files, 32, &count) == FLASH_OK) {
     *     for (uint8_t i = 0; i < count; i++) {
     *         LOG_INFO("File: %s (%lu bytes)", files[i].filename, files[i].size);
     *     }
     * }
     * ```
     */
    FlashStorageStatus listNodeFiles(const char *nodeId, FlashFileInfo *fileList, uint8_t maxFiles, uint8_t *fileCount);

    // ========================================================================
    // Storage Maintenance Methods
    // ========================================================================

    /**
     * @brief Clean up old files to free storage space
     *
     * Automatically deletes oldest files until target free space is achieved.
     * Files are selected based on filename (assumes date-based naming where
     * alphabetically earlier names are older).
     *
     * This function performs:
     * 1. Checks if cleanup is needed
     * 2. Lists files in specified node (or all nodes)
     * 3. Deletes files starting from oldest
     * 4. Continues until target free space reached or no files remain
     *
     * Pre-conditions:
     * - Storage must be initialized
     *
     * Post-conditions (on success):
     * - Free space >= targetFreeBytes (if possible)
     * - Oldest files deleted first
     *
     * @param nodeId Node ID to clean up, or null to clean all nodes.
     * @param targetFreeBytes Target free space to achieve in bytes.
     *
     * @return FlashStorageStatus
     *         - FLASH_OK: Cleanup completed (target may or may not be reached)
     *         - FLASH_ERR_NOT_INITIALIZED: Storage not initialized
     *         - FLASH_ERR_INVALID_PARAM: Invalid nodeId format
     *         - FLASH_ERR_READ_FAILED: Could not read directory
     *
     * @note Uses bounded iteration per NASA Rule 2
     * @note Best-effort: may not reach target if insufficient files to delete
     *
     * Example:
     * ```cpp
     * // Ensure at least 100KB free
     * storage.cleanupOldFiles(nullptr, 100 * 1024);
     * ```
     */
    FlashStorageStatus cleanupOldFiles(const char *nodeId, uint32_t targetFreeBytes);

  private:
    // ========================================================================
    // Private Member Variables
    // ========================================================================

    bool initialized; ///< True after successful begin() call

    /// Static path buffer for building file paths (Rule 3: no dynamic allocation)
    char pathBuffer[FLASH_MAX_PATH_LENGTH];

    // ========================================================================
    // Private Helper Methods
    // ========================================================================

    /**
     * @brief Build full filesystem path from nodeId and filename
     *
     * Constructs a path string in the format "/<nodeId>/<filename>" or
     * "/<nodeId>" if filename is null.
     *
     * Pre-conditions:
     * - nodeId must not be null
     * - outPath must not be null
     * - outPathLen must be > 0
     *
     * Post-conditions (on success):
     * - outPath contains null-terminated path string
     *
     * @param nodeId Node identifier string
     * @param filename Filename string, or null for directory-only path
     * @param outPath Output buffer for constructed path
     * @param outPathLen Size of output buffer
     *
     * @return true if path built successfully, false if path too long
     */
    bool buildPath(const char *nodeId, const char *filename, char *outPath, uint16_t outPathLen);

    /**
     * @brief Validate node ID string format
     *
     * Checks that nodeId contains only hexadecimal characters (0-9, A-F, a-f)
     * and is between 1-8 characters in length.
     *
     * @param nodeId Node ID string to validate
     *
     * @return true if valid hex string of appropriate length
     */
    bool isValidNodeId(const char *nodeId);

    /**
     * @brief Validate filename string format
     *
     * Checks that filename:
     * - Is not null or empty
     * - Is less than FLASH_MAX_FILENAME_LENGTH
     * - Contains no path separators (/ or \)
     * - Contains no control characters
     *
     * @param filename Filename string to validate
     *
     * @return true if valid filename
     */
    bool isValidFilename(const char *filename);

    /**
     * @brief Ensure node directory exists, creating if necessary
     *
     * Checks for directory existence and creates it if missing.
     *
     * @param nodeId Node ID for directory name
     *
     * @return true if directory exists or was created successfully
     */
    bool ensureDirectoryExists(const char *nodeId);

    /**
     * @brief Calculate string length with safety bound (NASA Rule 2)
     *
     * Returns length of string, stopping at maxLen if string is longer.
     * Prevents unbounded iteration on unterminated strings.
     *
     * @param str String to measure (may be null)
     * @param maxLen Maximum length to scan
     *
     * @return String length, or 0 if null, capped at maxLen
     */
    uint16_t safeStrLen(const char *str, uint16_t maxLen);

    /**
     * @brief Copy string with bounds checking (NASA Rule 2)
     *
     * Safely copies src to dest, ensuring null termination and
     * preventing buffer overflow.
     *
     * @param dest Destination buffer
     * @param src Source string
     * @param destSize Size of destination buffer
     */
    void safeStrCopy(char *dest, const char *src, uint16_t destSize);

    /**
     * @brief Delete directory and all contents recursively
     *
     * Removes all files within a directory, then removes the directory.
     * Uses bounded iteration to comply with NASA Rule 2.
     *
     * @param path Path to directory to delete
     *
     * @return true if directory and all contents deleted
     */
    bool deleteDirectoryRecursive(const char *path);
};

#endif // HELTEC_V4 || HAS_FLASH_STORAGE
