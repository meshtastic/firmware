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

// Storage status codes
enum FlashStorageStatus {
    FLASH_OK = 0,
    FLASH_ERR_NOT_INITIALIZED = 1,
    FLASH_ERR_INVALID_PARAM = 2,
    FLASH_ERR_FILE_NOT_FOUND = 3,
    FLASH_ERR_DIR_NOT_FOUND = 4,
    FLASH_ERR_STORAGE_FULL = 5,
    FLASH_ERR_WRITE_FAILED = 6,
    FLASH_ERR_READ_FAILED = 7,
    FLASH_ERR_DELETE_FAILED = 8,
    FLASH_ERR_PATH_TOO_LONG = 9,
    FLASH_ERR_BUFFER_TOO_SMALL = 10,
    FLASH_ERR_FORMAT_FAILED = 11
};

/**
 * @brief File information structure (no dynamic allocation)
 */
struct FlashFileInfo {
    char filename[FLASH_MAX_FILENAME_LENGTH];
    uint32_t size;
    bool isDirectory;
};

/**
 * @brief Storage statistics structure
 */
struct FlashStorageStats {
    uint32_t totalBytes;
    uint32_t usedBytes;
    uint32_t freeBytes;
    uint16_t totalFiles;
    uint16_t totalDirectories;
};

/**
 * @brief NASA Power of 10 compliant flash storage class for Heltec V4
 *
 * Provides file-based storage with wear leveling for mesh node data.
 * Directory structure: /<nodeId>/<filename>
 * Example: /AABBCCDD/2024-01-15.dat
 */
class HeltecFlashStorage
{
  public:
    /**
     * @brief Construct storage manager
     * No dynamic allocation in constructor
     */
    HeltecFlashStorage();

    /**
     * @brief Initialize the flash storage system
     *
     * @param formatOnFail If true, format storage if mount fails
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus begin(bool formatOnFail = true);

    /**
     * @brief Format the entire flash storage (erase all data)
     *
     * Thread-safe operation
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus format();

    /**
     * @brief Create a file for a specific node
     *
     * Creates directory if it doesn't exist.
     * Directory structure: /<nodeId>/<filename>
     *
     * @param nodeId Node ID as hex string (max 8 chars)
     * @param filename Filename to create (max 31 chars)
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus createFile(const char *nodeId, const char *filename);

    /**
     * @brief Delete a specific file
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to delete
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus deleteFile(const char *nodeId, const char *filename);

    /**
     * @brief Delete all files for a specific node (delete directory)
     *
     * @param nodeId Node ID as hex string
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus deleteNodeDirectory(const char *nodeId);

    /**
     * @brief Read file content into buffer
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to read
     * @param buffer Output buffer (must not be null)
     * @param maxLength Maximum bytes to read
     * @param bytesRead Output: actual bytes read (must not be null)
     * @param offset Offset in file to start reading from
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus readFile(const char *nodeId, const char *filename, uint8_t *buffer, uint16_t maxLength,
                                uint16_t *bytesRead, uint32_t offset = 0);

    /**
     * @brief Write data to file (overwrites existing content)
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to write
     * @param data Data to write (must not be null)
     * @param length Number of bytes to write
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus writeFile(const char *nodeId, const char *filename, const uint8_t *data, uint16_t length);

    /**
     * @brief Append data to existing file
     *
     * Creates file if it doesn't exist.
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to append to
     * @param data Data to append (must not be null)
     * @param length Number of bytes to append
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus appendFile(const char *nodeId, const char *filename, const uint8_t *data, uint16_t length);

    /**
     * @brief Edit file content at specific offset
     *
     * Does not change file size. Offset + length must be within file.
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to edit
     * @param offset Byte offset to start editing
     * @param data New data to write at offset
     * @param length Number of bytes to write
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus editFile(const char *nodeId, const char *filename, uint32_t offset, const uint8_t *data,
                                uint16_t length);

    /**
     * @brief Check if a file exists
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to check
     * @return true if file exists
     */
    bool fileExists(const char *nodeId, const char *filename);

    /**
     * @brief Check if a node directory exists
     *
     * @param nodeId Node ID as hex string
     * @return true if directory exists
     */
    bool nodeDirectoryExists(const char *nodeId);

    /**
     * @brief Get file size
     *
     * @param nodeId Node ID as hex string
     * @param filename Filename to check
     * @param fileSize Output: file size in bytes (must not be null)
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus getFileSize(const char *nodeId, const char *filename, uint32_t *fileSize);

    /**
     * @brief Get storage statistics
     *
     * @param stats Output: storage statistics (must not be null)
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus getStorageStats(FlashStorageStats *stats);

    /**
     * @brief Get available storage space in bytes
     *
     * @return Available bytes, or 0 if not initialized
     */
    uint32_t getAvailableSpace();

    /**
     * @brief Get used storage space in bytes
     *
     * @return Used bytes, or 0 if not initialized
     */
    uint32_t getUsedSpace();

    /**
     * @brief Get total storage capacity in bytes
     *
     * @return Total capacity, or 0 if not initialized
     */
    uint32_t getTotalSpace();

    /**
     * @brief Check if storage is initialized and ready
     *
     * @return true if ready for use
     */
    bool isInitialized() const { return initialized; }

    /**
     * @brief List files in a node directory
     *
     * @param nodeId Node ID as hex string
     * @param fileList Output array of file info structures
     * @param maxFiles Maximum files to list (array size)
     * @param fileCount Output: actual number of files found
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus listNodeFiles(const char *nodeId, FlashFileInfo *fileList, uint8_t maxFiles, uint8_t *fileCount);

    /**
     * @brief Clean up old files when storage is near full
     *
     * Deletes oldest files first based on filename (date-based naming assumed)
     *
     * @param nodeId Node ID to clean up (null for all nodes)
     * @param targetFreeBytes Target free space to achieve
     * @return FlashStorageStatus result code
     */
    FlashStorageStatus cleanupOldFiles(const char *nodeId, uint32_t targetFreeBytes);

  private:
    bool initialized;

    // Static path buffer (Rule 3: no dynamic allocation)
    char pathBuffer[FLASH_MAX_PATH_LENGTH];

    /**
     * @brief Build full path from nodeId and filename
     *
     * @param nodeId Node ID string
     * @param filename Filename string (can be null for directory path)
     * @param outPath Output path buffer
     * @param outPathLen Size of output buffer
     * @return true if path was built successfully
     */
    bool buildPath(const char *nodeId, const char *filename, char *outPath, uint16_t outPathLen);

    /**
     * @brief Validate node ID string
     *
     * @param nodeId Node ID to validate
     * @return true if valid
     */
    bool isValidNodeId(const char *nodeId);

    /**
     * @brief Validate filename string
     *
     * @param filename Filename to validate
     * @return true if valid
     */
    bool isValidFilename(const char *filename);

    /**
     * @brief Ensure directory exists, create if needed
     *
     * @param nodeId Node ID for directory
     * @return true if directory exists or was created
     */
    bool ensureDirectoryExists(const char *nodeId);

    /**
     * @brief Get string length with maximum bound (Rule 2)
     *
     * @param str String to measure
     * @param maxLen Maximum length to check
     * @return Length of string, or maxLen if longer
     */
    uint16_t safeStrLen(const char *str, uint16_t maxLen);

    /**
     * @brief Safe string copy with bounds checking (Rule 2)
     *
     * @param dest Destination buffer
     * @param src Source string
     * @param destSize Size of destination buffer
     */
    void safeStrCopy(char *dest, const char *src, uint16_t destSize);

    /**
     * @brief Delete directory and all contents (helper with bounded iteration)
     *
     * @param path Directory path
     * @return true if deleted successfully
     */
    bool deleteDirectoryRecursive(const char *path);
};

#endif // HELTEC_V4 || HAS_FLASH_STORAGE
