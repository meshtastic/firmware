#pragma once

#include "configuration.h"

#if __has_include(<Adafruit_FRAM_SPI.h>)

#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <Adafruit_FRAM_SPI.h>
#include <SPI.h>

/**
 * @brief Multi-core safe FRAM batch storage for XIAO RP2350
 *
 * This class provides thread-safe access to FRAM memory connected via SPI1.
 * Designed for dual-core operation where:
 * - Core 1: Writes batches of data
 * - Core 0: Reads and deletes batches after processing
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

// Default FRAM size (256Kbit = 32KB)
#ifndef FRAM_SIZE_BYTES
#define FRAM_SIZE_BYTES 32768
#endif

// Header structure size
#define FRAM_HEADER_SIZE 16

// Batch entry header size (size + status)
#define BATCH_HEADER_SIZE 3

// Maximum single batch size
#ifndef FRAM_MAX_BATCH_SIZE
#define FRAM_MAX_BATCH_SIZE 256
#endif

// Batch status values
#define BATCH_STATUS_FREE 0x00
#define BATCH_STATUS_VALID 0x01
#define BATCH_STATUS_DELETED 0xFF

// FRAM header offsets
#define FRAM_OFFSET_MAGIC 0x00       // 2 bytes: Magic number
#define FRAM_OFFSET_VERSION 0x02     // 1 byte: Version
#define FRAM_OFFSET_BATCH_COUNT 0x03 // 1 byte: Number of valid batches
#define FRAM_OFFSET_HEAD 0x04        // 4 bytes: Head pointer (next write position)
#define FRAM_OFFSET_TAIL 0x08        // 4 bytes: Tail pointer (next read position)
#define FRAM_OFFSET_FLAGS 0x0C       // 4 bytes: Flags

// Magic number to verify FRAM is initialized
#define FRAM_MAGIC_NUMBER 0x4652 // "FR"
#define FRAM_VERSION 0x01

class FRAMBatchStorage
{
  public:
    /**
     * @brief Construct a new FRAMBatchStorage object
     *
     * @param csPin Chip select pin for FRAM
     * @param spi Pointer to SPI instance (default SPI1 on XIAO RP2350)
     * @param spiFreq SPI clock frequency in Hz (default 8MHz)
     */
    FRAMBatchStorage(int8_t csPin, SPIClass *spi = &SPI1, uint32_t spiFreq = 8000000);

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
     *
     * @param data Pointer to data buffer
     * @param length Number of bytes to write (max FRAM_MAX_BATCH_SIZE)
     * @return true if write succeeded
     */
    bool writeBatch(const uint8_t *data, uint16_t length);

    /**
     * @brief Read the next available batch from FRAM (typically called from Core 0)
     *
     * Thread-safe: Uses SPI lock for mutual exclusion
     * Does not delete the batch - call deleteBatch() after processing
     *
     * @param buffer Buffer to store read data
     * @param maxLength Maximum bytes to read
     * @param actualLength Output: actual bytes read
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
     * @brief Get FRAM device ID information
     *
     * @param manufacturerID Output: Manufacturer ID
     * @param productID Output: Product ID
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

    // Storage boundaries
    uint32_t dataStartAddr;
    uint32_t dataEndAddr;

    /**
     * @brief Read the header from FRAM
     *
     * Caller must hold spiLock
     *
     * @return true if header is valid
     */
    bool readHeader();

    /**
     * @brief Write the header to FRAM
     *
     * Caller must hold spiLock
     *
     * @return true if write succeeded
     */
    bool writeHeader();

    /**
     * @brief Initialize header with default values
     *
     * Caller must hold spiLock
     *
     * @return true if initialization succeeded
     */
    bool initHeader();

    /**
     * @brief Calculate wrapped address in circular buffer
     *
     * @param addr Address to wrap
     * @return Wrapped address within data region
     */
    uint32_t wrapAddress(uint32_t addr);

    /**
     * @brief Check if there's enough space for a new batch
     *
     * IMPORTANT: Caller must already hold spiLock - this method does not acquire it
     *
     * @param size Size of batch including header
     * @return true if enough space exists
     */
    bool hasSpaceFor(uint16_t size);

    /**
     * @brief Write data to FRAM with address wrapping
     *
     * Caller must hold spiLock
     *
     * @param addr Starting address
     * @param data Data to write
     * @param length Number of bytes
     * @return Address after last written byte
     */
    uint32_t writeWithWrap(uint32_t addr, const uint8_t *data, uint16_t length);

    /**
     * @brief Read data from FRAM with address wrapping
     *
     * Caller must hold spiLock
     *
     * @param addr Starting address
     * @param buffer Buffer to store data
     * @param length Number of bytes
     * @return Address after last read byte
     */
    uint32_t readWithWrap(uint32_t addr, uint8_t *buffer, uint16_t length);
};

#endif // __has_include(<Adafruit_FRAM_SPI.h>)
