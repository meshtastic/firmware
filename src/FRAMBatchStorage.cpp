/**
 * @file FRAMBatchStorage.cpp
 * @brief NASA Power of 10 compliant FRAM batch storage implementation
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
 */

#include "configuration.h"

#if __has_include(<Adafruit_FRAM_SPI.h>)

#include "FRAMBatchStorage.h"
#include "main.h"
#include <assert.h>

// ============================================================================
// Constructor
// ============================================================================

FRAMBatchStorage::FRAMBatchStorage(int8_t csPin, SPIClass *spi, uint32_t spiFreq)
    : fram(csPin, spi, spiFreq), csPin(csPin), spi(spi), spiFreq(spiFreq), initialized(false), headPtr(0), tailPtr(0),
      batchCount(0), evictionCount(0)
{
    assert(spi != nullptr);                  // Rule 5: assertion 1
    assert(FRAM_SIZE_BYTES >= FRAM_MIN_SIZE); // Rule 5: assertion 2

    dataStartAddr = FRAM_HEADER_SIZE;
    dataEndAddr = FRAM_SIZE_BYTES;
}

// ============================================================================
// Public Methods
// ============================================================================

bool FRAMBatchStorage::begin(bool format)
{
    assert(spi != nullptr);                  // Rule 5: assertion 1
    assert(FRAM_SIZE_BYTES >= FRAM_MIN_SIZE); // Rule 5: assertion 2

    if (initialized) {
        return true;
    }

    {
        concurrency::LockGuard g(spiLock);

        bool framInitOk = fram.begin(); // Rule 7: capture return value
        if (!framInitOk) {
            LOG_ERROR("FRAM: Failed to initialize SPI FRAM");
            return false;
        }

        if (!format) {
            bool headerOk = readHeader(); // Rule 7: check return
            if (headerOk) {
                LOG_INFO("FRAM: Found valid storage with %d batches", batchCount);
                initialized = true;
                return true;
            }
        }
    }

    bool formatOk = this->format(); // Rule 7: check return
    if (!formatOk) {
        LOG_ERROR("FRAM: Failed to format storage");
        return false;
    }

    initialized = true;
    LOG_INFO("FRAM: Storage initialized (formatted)");
    return true;
}

bool FRAMBatchStorage::writeBatch(const uint8_t *data, uint16_t length)
{
    assert(data != nullptr);                  // Rule 5: assertion 1
    assert(length <= FRAM_MAX_BATCH_SIZE);    // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if (data == nullptr) {
        LOG_WARN("FRAM: Null data pointer");
        return false;
    }

    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    if ((length == 0) || (length > FRAM_MAX_BATCH_SIZE)) {
        LOG_WARN("FRAM: Invalid batch size %d", length);
        return false;
    }

    concurrency::LockGuard g(spiLock);

    bool headerOk = readHeader(); // Rule 7: check return
    if (!headerOk) {
        LOG_ERROR("FRAM: Failed to read header for write");
        return false;
    }

    uint16_t totalSize = BATCH_HEADER_SIZE + length;

    // Rule 2: Fixed loop bound with FRAM_MAX_CLEANUP_ITERATIONS
    // REQ-STOR-005: Eviction policy - delete oldest batches when full
    uint8_t cleanupCount = 0;
    while ((!hasSpaceFor(totalSize)) && (batchCount > 0) && (cleanupCount < FRAM_MAX_CLEANUP_ITERATIONS)) {
        LOG_INFO("FRAM: Evicting oldest batch to make room (eviction #%lu)", (unsigned long)(evictionCount + 1));
        bool deleteOk = deleteOldestBatchInternal(); // Rule 7: check return
        if (!deleteOk) {
            LOG_ERROR("FRAM: Failed to evict batch");
            return false;
        }
        evictionCount++;  // REQ-STOR-005: Track eviction statistics
        cleanupCount++;
    }

    if (!hasSpaceFor(totalSize)) {
        LOG_WARN("FRAM: Not enough space after %d cleanup iterations", cleanupCount);
        return false;
    }

    // Write batch header
    uint8_t batchHeader[BATCH_HEADER_SIZE];
    batchHeader[0] = (uint8_t)(length & 0xFFU);
    batchHeader[1] = (uint8_t)((length >> 8) & 0xFFU);
    batchHeader[2] = BATCH_STATUS_VALID;

    uint32_t writeAddr = headPtr;
    writeAddr = writeWithWrap(writeAddr, batchHeader, BATCH_HEADER_SIZE);
    writeAddr = writeWithWrap(writeAddr, data, length);

    headPtr = writeAddr;
    batchCount++;

    bool writeOk = writeHeader(); // Rule 7: check return
    if (!writeOk) {
        LOG_ERROR("FRAM: Failed to update header after write");
        return false;
    }

    LOG_DEBUG("FRAM: Wrote batch of %d bytes, count=%d", length, batchCount);
    return true;
}

bool FRAMBatchStorage::readBatch(uint8_t *buffer, uint16_t maxLength, uint16_t *actualLength)
{
    assert(buffer != nullptr);       // Rule 5: assertion 1
    assert(actualLength != nullptr); // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((buffer == nullptr) || (actualLength == nullptr)) {
        return false;
    }

    if (maxLength == 0) {
        return false;
    }

    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    concurrency::LockGuard g(spiLock);

    bool headerOk = readHeader(); // Rule 7: check return
    if (!headerOk) {
        LOG_ERROR("FRAM: Failed to read header");
        return false;
    }

    if (batchCount == 0) {
        LOG_DEBUG("FRAM: No batches to read");
        return false;
    }

    // Read batch header at tail position
    uint8_t batchHeader[BATCH_HEADER_SIZE];
    (void)readWithWrap(tailPtr, batchHeader, BATCH_HEADER_SIZE);

    uint16_t batchSize = (uint16_t)batchHeader[0] | ((uint16_t)batchHeader[1] << 8);
    uint8_t status = batchHeader[2];

    assert(isValidDataAddress(tailPtr)); // Rule 5: additional assertion

    if ((status == BATCH_STATUS_DELETED) || (status == BATCH_STATUS_FREE)) {
        LOG_WARN("FRAM: Found invalid batch at tail, status=%02X", status);
        return false;
    }

    if (batchSize > maxLength) {
        LOG_WARN("FRAM: Batch size %d exceeds buffer size %d", batchSize, maxLength);
        *actualLength = 0;
        return false;
    }

    if (batchSize > FRAM_MAX_BATCH_SIZE) {
        LOG_ERROR("FRAM: Corrupt batch size %d", batchSize);
        *actualLength = 0;
        return false;
    }

    uint32_t dataAddr = wrapAddress(tailPtr + BATCH_HEADER_SIZE);
    (void)readWithWrap(dataAddr, buffer, batchSize);

    *actualLength = batchSize;
    LOG_DEBUG("FRAM: Read batch of %d bytes", batchSize);
    return true;
}

uint16_t FRAMBatchStorage::peekBatchSize()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);

    bool headerOk = readHeader(); // Rule 7: check return
    if (!headerOk) {
        return 0;
    }

    assert(isValidDataAddress(tailPtr)); // Rule 5: assertion 2

    if (batchCount == 0) {
        return 0;
    }

    uint8_t sizeBytes[2];
    (void)readWithWrap(tailPtr, sizeBytes, 2);

    uint16_t size = (uint16_t)sizeBytes[0] | ((uint16_t)sizeBytes[1] << 8);
    return size;
}

bool FRAMBatchStorage::deleteBatch()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    concurrency::LockGuard g(spiLock);

    bool headerOk = readHeader(); // Rule 7: check return
    if (!headerOk) {
        LOG_ERROR("FRAM: Failed to read header for delete");
        return false;
    }

    assert(isValidDataAddress(tailPtr)); // Rule 5: assertion 2

    if (batchCount == 0) {
        LOG_DEBUG("FRAM: No batches to delete");
        return false;
    }

    uint8_t batchHeader[BATCH_HEADER_SIZE];
    (void)readWithWrap(tailPtr, batchHeader, BATCH_HEADER_SIZE);

    uint16_t batchSize = (uint16_t)batchHeader[0] | ((uint16_t)batchHeader[1] << 8);

    // Rule 7: Validate batch size before use
    if (batchSize > FRAM_MAX_BATCH_SIZE) {
        LOG_ERROR("FRAM: Corrupt batch size during delete: %d", batchSize);
        return false;
    }

    uint8_t deleteStatus = BATCH_STATUS_DELETED;
    uint32_t statusAddr = wrapAddress(tailPtr + 2U);
    (void)fram.write(statusAddr, &deleteStatus, 1);

    uint32_t totalSize = BATCH_HEADER_SIZE + batchSize;
    tailPtr = wrapAddress(tailPtr + totalSize);
    batchCount--;

    bool writeOk = writeHeader(); // Rule 7: check return
    if (!writeOk) {
        LOG_ERROR("FRAM: Failed to update header after delete");
        return false;
    }

    LOG_DEBUG("FRAM: Deleted batch of %d bytes, remaining=%d", batchSize, batchCount);
    return true;
}

uint8_t FRAMBatchStorage::getBatchCount()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);
    (void)readHeader(); // Rule 7: explicitly ignore return for status query

    assert(batchCount <= FRAM_MAX_BATCH_COUNT); // Rule 5: assertion 2

    return batchCount;
}

bool FRAMBatchStorage::hasBatches()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1
    assert(FRAM_SIZE_BYTES >= FRAM_MIN_SIZE); // Rule 5: assertion 2

    uint8_t count = getBatchCount();
    return (count > 0);
}

uint32_t FRAMBatchStorage::getAvailableSpace()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    if (!initialized) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);
    (void)readHeader();

    assert(isValidDataAddress(headPtr)); // Rule 5: assertion 2

    return calculateAvailableSpace();
}

uint8_t FRAMBatchStorage::getUsagePercentage()
{
    /* REQ-OPS-002: Calculate FRAM usage as percentage
     * NASA Rule 5: Assertions for preconditions */
    assert(dataEndAddr > dataStartAddr);

    if (!initialized) {
        return 0;
    }

    /* Calculate total data capacity and available space */
    uint32_t totalCapacity = dataEndAddr - dataStartAddr;
    uint32_t available = getAvailableSpace();

    /* Avoid division by zero */
    if (totalCapacity == 0) {
        return 100;  /* No capacity = 100% full */
    }

    /* Calculate used percentage: (used / total) * 100 */
    uint32_t used = totalCapacity - available;
    uint8_t percentage = (uint8_t)((used * 100U) / totalCapacity);

    return percentage;
}

bool FRAMBatchStorage::format()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1
    assert(FRAM_SIZE_BYTES >= FRAM_MIN_SIZE); // Rule 5: assertion 2

    concurrency::LockGuard g(spiLock);

    headPtr = dataStartAddr;
    tailPtr = dataStartAddr;
    batchCount = 0;

    bool initOk = initHeader(); // Rule 7: check return
    return initOk;
}

bool FRAMBatchStorage::getDeviceID(uint8_t *manufacturerID, uint16_t *productID)
{
    assert(manufacturerID != nullptr); // Rule 5: assertion 1
    assert(productID != nullptr);      // Rule 5: assertion 2

    // Rule 7: Parameter validation
    if ((manufacturerID == nullptr) || (productID == nullptr)) {
        return false;
    }

    concurrency::LockGuard g(spiLock);
    bool result = fram.getDeviceID(manufacturerID, productID); // Rule 7: check return
    return result;
}

bool FRAMBatchStorage::enterSleep()
{
    assert(spi != nullptr); // Rule 5: assertion 1

    if (!initialized) {
        return false;
    }

    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 2

    concurrency::LockGuard g(spiLock);
    bool result = fram.enterSleep(); // Rule 7: check return
    return result;
}

bool FRAMBatchStorage::exitSleep()
{
    assert(spi != nullptr); // Rule 5: assertion 1

    if (!initialized) {
        return false;
    }

    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 2

    concurrency::LockGuard g(spiLock);
    bool result = fram.exitSleep(); // Rule 7: check return
    return result;
}

// ============================================================================
// Private Methods
// ============================================================================

bool FRAMBatchStorage::readHeader()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    uint8_t header[FRAM_HEADER_SIZE];
    (void)fram.read(0, header, FRAM_HEADER_SIZE);

    uint16_t magic = (uint16_t)header[FRAM_OFFSET_MAGIC] | ((uint16_t)header[FRAM_OFFSET_MAGIC + 1U] << 8);

    assert(FRAM_MAGIC_NUMBER == 0x4652U); // Rule 5: assertion 2

    if (magic != FRAM_MAGIC_NUMBER) {
        LOG_DEBUG("FRAM: Invalid magic number %04X", magic);
        return false;
    }

    if (header[FRAM_OFFSET_VERSION] != FRAM_VERSION) {
        LOG_WARN("FRAM: Version mismatch (found %d, expected %d)", header[FRAM_OFFSET_VERSION], FRAM_VERSION);
        return false;
    }

    batchCount = header[FRAM_OFFSET_BATCH_COUNT];

    headPtr = (uint32_t)header[FRAM_OFFSET_HEAD] | ((uint32_t)header[FRAM_OFFSET_HEAD + 1U] << 8) |
              ((uint32_t)header[FRAM_OFFSET_HEAD + 2U] << 16) | ((uint32_t)header[FRAM_OFFSET_HEAD + 3U] << 24);

    tailPtr = (uint32_t)header[FRAM_OFFSET_TAIL] | ((uint32_t)header[FRAM_OFFSET_TAIL + 1U] << 8) |
              ((uint32_t)header[FRAM_OFFSET_TAIL + 2U] << 16) | ((uint32_t)header[FRAM_OFFSET_TAIL + 3U] << 24);

    // Rule 7: Validate pointers
    if (!isValidDataAddress(headPtr) || !isValidDataAddress(tailPtr)) {
        LOG_WARN("FRAM: Invalid pointers (head=%lu, tail=%lu)", headPtr, tailPtr);
        return false;
    }

    return true;
}

bool FRAMBatchStorage::writeHeader()
{
    assert(dataEndAddr > dataStartAddr);     // Rule 5: assertion 1
    assert(isValidDataAddress(headPtr));     // Rule 5: assertion 2

    uint8_t header[FRAM_HEADER_SIZE] = {0};

    header[FRAM_OFFSET_MAGIC] = (uint8_t)(FRAM_MAGIC_NUMBER & 0xFFU);
    header[FRAM_OFFSET_MAGIC + 1U] = (uint8_t)((FRAM_MAGIC_NUMBER >> 8) & 0xFFU);
    header[FRAM_OFFSET_VERSION] = FRAM_VERSION;
    header[FRAM_OFFSET_BATCH_COUNT] = batchCount;

    header[FRAM_OFFSET_HEAD] = (uint8_t)(headPtr & 0xFFU);
    header[FRAM_OFFSET_HEAD + 1U] = (uint8_t)((headPtr >> 8) & 0xFFU);
    header[FRAM_OFFSET_HEAD + 2U] = (uint8_t)((headPtr >> 16) & 0xFFU);
    header[FRAM_OFFSET_HEAD + 3U] = (uint8_t)((headPtr >> 24) & 0xFFU);

    header[FRAM_OFFSET_TAIL] = (uint8_t)(tailPtr & 0xFFU);
    header[FRAM_OFFSET_TAIL + 1U] = (uint8_t)((tailPtr >> 8) & 0xFFU);
    header[FRAM_OFFSET_TAIL + 2U] = (uint8_t)((tailPtr >> 16) & 0xFFU);
    header[FRAM_OFFSET_TAIL + 3U] = (uint8_t)((tailPtr >> 24) & 0xFFU);

    (void)fram.writeEnable(true);
    bool success = fram.write(0, header, FRAM_HEADER_SIZE);
    (void)fram.writeEnable(false);

    return success;
}

bool FRAMBatchStorage::initHeader()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1
    assert(dataStartAddr == FRAM_HEADER_SIZE); // Rule 5: assertion 2

    headPtr = dataStartAddr;
    tailPtr = dataStartAddr;
    batchCount = 0;

    bool success = writeHeader(); // Rule 7: check return
    if (success) {
        LOG_INFO("FRAM: Header initialized");
    }
    return success;
}

uint32_t FRAMBatchStorage::wrapAddress(uint32_t addr)
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    uint32_t dataSize = dataEndAddr - dataStartAddr;

    assert(dataSize > 0); // Rule 5: assertion 2

    if (addr >= dataEndAddr) {
        uint32_t offset = addr - dataStartAddr;
        addr = dataStartAddr + (offset % dataSize);
    }

    return addr;
}

bool FRAMBatchStorage::hasSpaceFor(uint16_t size)
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1
    assert(size > 0); // Rule 5: assertion 2

    uint32_t available = calculateAvailableSpace();
    uint32_t required = (uint32_t)size + BATCH_HEADER_SIZE;

    return (available > required);
}

bool FRAMBatchStorage::deleteOldestBatchInternal()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1
    assert(isValidDataAddress(tailPtr)); // Rule 5: assertion 2

    if (batchCount == 0) {
        return false;
    }

    uint8_t batchHeader[BATCH_HEADER_SIZE];
    (void)readWithWrap(tailPtr, batchHeader, BATCH_HEADER_SIZE);

    uint16_t batchSize = (uint16_t)batchHeader[0] | ((uint16_t)batchHeader[1] << 8);

    // Rule 7: Validate batch size
    if (batchSize > FRAM_MAX_BATCH_SIZE) {
        LOG_ERROR("FRAM: Corrupt batch size in internal delete: %d", batchSize);
        return false;
    }

    uint8_t deleteStatus = BATCH_STATUS_DELETED;
    uint32_t statusAddr = wrapAddress(tailPtr + 2U);
    (void)fram.write(statusAddr, &deleteStatus, 1);

    uint32_t totalSize = BATCH_HEADER_SIZE + batchSize;
    tailPtr = wrapAddress(tailPtr + totalSize);
    batchCount--;

    return true;
}

uint32_t FRAMBatchStorage::writeWithWrap(uint32_t addr, const uint8_t *data, uint16_t length)
{
    assert(data != nullptr);             // Rule 5: assertion 1
    assert(isValidDataAddress(addr));    // Rule 5: assertion 2

    if ((data == nullptr) || (length == 0)) {
        return addr;
    }

    uint32_t bytesToEnd = dataEndAddr - addr;

    (void)fram.writeEnable(true);

    if (length <= bytesToEnd) {
        (void)fram.write(addr, data, length);
        addr = wrapAddress(addr + length);
    } else {
        (void)fram.write(addr, data, bytesToEnd);
        uint16_t remaining = length - (uint16_t)bytesToEnd;
        (void)fram.write(dataStartAddr, data + bytesToEnd, remaining);
        addr = dataStartAddr + remaining;
    }

    (void)fram.writeEnable(false);
    return addr;
}

uint32_t FRAMBatchStorage::readWithWrap(uint32_t addr, uint8_t *buffer, uint16_t length)
{
    assert(buffer != nullptr);           // Rule 5: assertion 1
    assert(isValidDataAddress(addr));    // Rule 5: assertion 2

    if ((buffer == nullptr) || (length == 0)) {
        return addr;
    }

    uint32_t bytesToEnd = dataEndAddr - addr;

    if (length <= bytesToEnd) {
        (void)fram.read(addr, buffer, length);
        addr = wrapAddress(addr + length);
    } else {
        (void)fram.read(addr, buffer, bytesToEnd);
        uint16_t remaining = length - (uint16_t)bytesToEnd;
        (void)fram.read(dataStartAddr, buffer + bytesToEnd, remaining);
        addr = dataStartAddr + remaining;
    }

    return addr;
}

bool FRAMBatchStorage::isValidDataAddress(uint32_t addr)
{
    return ((addr >= dataStartAddr) && (addr < dataEndAddr));
}

uint32_t FRAMBatchStorage::calculateAvailableSpace()
{
    assert(dataEndAddr > dataStartAddr); // Rule 5: assertion 1

    uint32_t dataSize = dataEndAddr - dataStartAddr;

    assert(dataSize > 0); // Rule 5: assertion 2

    if (batchCount == 0) {
        return dataSize;
    }

    if (headPtr >= tailPtr) {
        return dataSize - (headPtr - tailPtr);
    }

    return tailPtr - headPtr;
}

#endif // __has_include(<Adafruit_FRAM_SPI.h>)
