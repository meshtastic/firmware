#pragma once

#include "configuration.h"

#if __has_include(<Adafruit_FRAM_SPI.h>)

#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <Adafruit_FRAM_SPI.h>
#include <SPI.h>
#include <assert.h>

/**
 * @brief Multi-core safe FRAM batch storage for XIAO RP2350
 *
 * NASA Power of 10 Compliant Implementation
 *
 * This class provides thread-safe access to FRAM memory connected via SPI0.
 * Designed for dual-core operation where:
 * - Core 1: Writes batches of data (keystroke buffers)
 * - Core 0: Reads and deletes batches after processing/transmission
 *
 * Memory layout:
 * [0x0000 - 0x000F]: Header (batch count, head pointer, tail pointer, flags)
 * [0x0010 - END]:    Batch data storage (circular buffer)
 *
 * Each batch entry:
 * [2 bytes]: Batch size (uint16_t)
 * [1 byte]:  Status (0x00=free, 0x01=valid, 0xFF=deleted)
 * [N bytes]: Batch data
 */

// Default FRAM size (2Mbit = 256KB for larger FRAM chips)
#ifndef FRAM_SIZE_BYTES
#define FRAM_SIZE_BYTES 262144U  // 256KB default (adjust for your chip)
#endif

// Header structure size
#define FRAM_HEADER_SIZE 16U

// Batch entry header size (size + status)
#define BATCH_HEADER_SIZE 3U

// Maximum single batch size - increased to 512B for keystroke buffers
#ifndef FRAM_MAX_BATCH_SIZE
#define FRAM_MAX_BATCH_SIZE 512U
#endif

// Maximum number of batches (Rule 2: fixed loop bound)
// With 256KB FRAM and 512-byte batches: ~500 max batches
// We use a conservative limit for safety
#ifndef FRAM_MAX_BATCH_COUNT
#define FRAM_MAX_BATCH_COUNT 255U
#endif

// Maximum cleanup iterations per write (Rule 2: fixed loop bound)
#define FRAM_MAX_CLEANUP_ITERATIONS 16U

// FRAM Capacity Alerting Thresholds (REQ-OPS-002)
// Percentage of FRAM used that triggers warnings
#define FRAM_CAPACITY_WARNING_PCT   75U   // Log warning at 75% full
#define FRAM_CAPACITY_CRITICAL_PCT  90U   // Log critical at 90% full
#define FRAM_CAPACITY_FULL_PCT      99U   // Log error at 99% full

// Batch status values
#define BATCH_STATUS_FREE 0x00U
#define BATCH_STATUS_VALID 0x01U
#define BATCH_STATUS_DELETED 0xFFU

// FRAM header offsets
#define FRAM_OFFSET_MAGIC 0x00U       // 2 bytes: Magic number
#define FRAM_OFFSET_VERSION 0x02U     // 1 byte: Version
#define FRAM_OFFSET_BATCH_COUNT 0x03U // 1 byte: Number of valid batches
#define FRAM_OFFSET_HEAD 0x04U        // 4 bytes: Head pointer (next write position)
#define FRAM_OFFSET_TAIL 0x08U        // 4 bytes: Tail pointer (next read position)
#define FRAM_OFFSET_FLAGS 0x0CU       // 4 bytes: Flags

// Magic number to verify FRAM is initialized
#define FRAM_MAGIC_NUMBER 0x4652U // "FR"
#define FRAM_VERSION 0x01U

// Minimum valid FRAM size
#define FRAM_MIN_SIZE (FRAM_HEADER_SIZE + BATCH_HEADER_SIZE + 1U)

class FRAMBatchStorage
{
  public:
    /**
     * @brief Construct a new FRAMBatchStorage object
     *
     * @param csPin Chip select pin for FRAM (must be valid GPIO)
     * @param spi Pointer to SPI instance (must not be null)
     * @param spiFreq SPI clock frequency in Hz (1-20MHz typical)
     */
    FRAMBatchStorage(int8_t csPin, SPIClass *spi, uint32_t spiFreq);

    /**
     * @brief Initialize the FRAM storage
     *
     * @param format If true, format the FRAM even if valid data exists
     * @return true if initialization succeeded
     */
    bool begin(bool format = false);

    /**
     * @brief Write a batch of data to FRAM (typically called from Core 1)
     *
     * Thread-safe: Uses SPI lock for mutual exclusion
     * Auto-cleanup: Deletes oldest batches if storage is full
     *
     * @param data Pointer to data buffer (must not be null)
     * @param length Number of bytes to write (1 to FRAM_MAX_BATCH_SIZE)
     * @return true if write succeeded
     */
    bool writeBatch(const uint8_t *data, uint16_t length);

    /**
     * @brief Read the next available batch from FRAM (typically called from Core 0)
     *
     * Thread-safe: Uses SPI lock for mutual exclusion
     * Does not delete the batch - call deleteBatch() after processing
     *
     * @param buffer Buffer to store read data (must not be null)
     * @param maxLength Maximum bytes to read (must be > 0)
     * @param actualLength Output: actual bytes read (must not be null)
     * @return true if a batch was read successfully
     */
    bool readBatch(uint8_t *buffer, uint16_t maxLength, uint16_t *actualLength);

    /**
     * @brief Peek at the next batch size without reading data
     *
     * @return Size of next batch, or 0 if no batches available
     */
    uint16_t peekBatchSize();

    /**
     * @brief Delete the oldest batch after processing (typically called from Core 0)
     *
     * Thread-safe: Uses SPI lock for mutual exclusion
     *
     * @return true if batch was deleted successfully
     */
    bool deleteBatch();

    /**
     * @brief Get the number of valid batches in storage
     *
     * @return Number of batches waiting to be processed
     */
    uint8_t getBatchCount();

    /**
     * @brief Check if storage has any batches to process
     *
     * @return true if at least one batch is available
     */
    bool hasBatches();

    /**
     * @brief Get available storage space in bytes
     *
     * @return Number of bytes available for new batches
     */
    uint32_t getAvailableSpace();

    /**
     * @brief Get FRAM usage percentage (REQ-OPS-002)
     *
     * @return Percentage of FRAM used (0-100)
     */
    uint8_t getUsagePercentage();

    /**
     * @brief Format the FRAM storage (erase all data)
     *
     * Thread-safe: Uses SPI lock for mutual exclusion
     *
     * @return true if format succeeded
     */
    bool format();

    /**
     * @brief Check if FRAM is properly initialized
     *
     * @return true if FRAM is ready for use
     */
    bool isInitialized() const { return initialized; }

    /**
     * @brief Get count of batches evicted due to storage full (REQ-STOR-005)
     *
     * @return Number of oldest batches evicted to make room for new ones
     */
    uint32_t getEvictionCount() const { return evictionCount; }

    /**
     * @brief Get FRAM device ID information
     *
     * @param manufacturerID Output: Manufacturer ID (must not be null)
     * @param productID Output: Product ID (must not be null)
     * @return true if IDs were read successfully
     */
    bool getDeviceID(uint8_t *manufacturerID, uint16_t *productID);

    /**
     * @brief Enter low-power sleep mode
     *
     * @return true if sleep mode was entered
     */
    bool enterSleep();

    /**
     * @brief Exit low-power sleep mode
     *
     * @return true if sleep mode was exited
     */
    bool exitSleep();

  private:
    Adafruit_FRAM_SPI fram;
    int8_t csPin;
    SPIClass *spi;
    uint32_t spiFreq;
    bool initialized;

    // Cached header values for performance
    uint32_t headPtr;
    uint32_t tailPtr;
    uint8_t batchCount;

    // REQ-STOR-005: Eviction statistics (batches deleted to make room)
    uint32_t evictionCount;

    // Storage boundaries (set once at construction)
    uint32_t dataStartAddr;
    uint32_t dataEndAddr;

    /**
     * @brief Read the header from FRAM (caller must hold spiLock)
     * @return true if header is valid
     */
    bool readHeader();

    /**
     * @brief Write the header to FRAM (caller must hold spiLock)
     * @return true if write succeeded
     */
    bool writeHeader();

    /**
     * @brief Initialize header with default values (caller must hold spiLock)
     * @return true if initialization succeeded
     */
    bool initHeader();

    /**
     * @brief Calculate wrapped address in circular buffer
     * @param addr Address to wrap
     * @return Wrapped address within data region
     */
    uint32_t wrapAddress(uint32_t addr);

    /**
     * @brief Check if there's enough space for a new batch (caller must hold spiLock)
     * @param size Size of batch including header
     * @return true if enough space exists
     */
    bool hasSpaceFor(uint16_t size);

    /**
     * @brief Delete the oldest batch internal (caller must hold spiLock)
     * @return true if batch was deleted successfully
     */
    bool deleteOldestBatchInternal();

    /**
     * @brief Write data to FRAM with address wrapping (caller must hold spiLock)
     * @param addr Starting address
     * @param data Data to write (must not be null)
     * @param length Number of bytes
     * @return Address after last written byte
     */
    uint32_t writeWithWrap(uint32_t addr, const uint8_t *data, uint16_t length);

    /**
     * @brief Read data from FRAM with address wrapping (caller must hold spiLock)
     * @param addr Starting address
     * @param buffer Buffer to store data (must not be null)
     * @param length Number of bytes
     * @return Address after last read byte
     */
    uint32_t readWithWrap(uint32_t addr, uint8_t *buffer, uint16_t length);

    /**
     * @brief Validate that an address is within the data region
     * @param addr Address to validate
     * @return true if address is valid
     */
    bool isValidDataAddress(uint32_t addr);

    /**
     * @brief Calculate available space (caller must hold spiLock, header must be current)
     * @return Available bytes in circular buffer
     */
    uint32_t calculateAvailableSpace();
};

#endif // __has_include(<Adafruit_FRAM_SPI.h>)
