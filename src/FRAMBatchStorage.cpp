#include "configuration.h"

#if __has_include(<Adafruit_FRAM_SPI.h>)

#include "FRAMBatchStorage.h"
#include "main.h"

FRAMBatchStorage::FRAMBatchStorage(int8_t csPin, SPIClass *spi, uint32_t spiFreq)
    : fram(csPin, spi, spiFreq), csPin(csPin), spi(spi), spiFreq(spiFreq), initialized(false), headPtr(0), tailPtr(0),
      batchCount(0)
{
    dataStartAddr = FRAM_HEADER_SIZE;
    dataEndAddr = FRAM_SIZE_BYTES;
}

bool FRAMBatchStorage::begin(bool format)
{
    if (initialized) {
        return true;
    }

    {
        concurrency::LockGuard g(spiLock);

        if (!fram.begin()) {
            LOG_ERROR("FRAM: Failed to initialize SPI FRAM");
            return false;
        }

        // Try to read existing header
        if (!format && readHeader()) {
            LOG_INFO("FRAM: Found valid storage with %d batches", batchCount);
            initialized = true;
            return true;
        }
    }

    // Format needed - initialize fresh
    if (!this->format()) {
        LOG_ERROR("FRAM: Failed to format storage");
        return false;
    }

    initialized = true;
    LOG_INFO("FRAM: Storage initialized (formatted)");
    return true;
}

bool FRAMBatchStorage::writeBatch(const uint8_t *data, uint16_t length)
{
    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    if (length == 0 || length > FRAM_MAX_BATCH_SIZE) {
        LOG_WARN("FRAM: Invalid batch size %d", length);
        return false;
    }

    concurrency::LockGuard g(spiLock);

    // Refresh header to get latest state
    if (!readHeader()) {
        LOG_ERROR("FRAM: Failed to read header for write");
        return false;
    }

    // Calculate total size needed (batch header + data)
    uint16_t totalSize = BATCH_HEADER_SIZE + length;

    if (!hasSpaceFor(totalSize)) {
        LOG_WARN("FRAM: Not enough space for batch (need %d bytes)", totalSize);
        return false;
    }

    // Write batch header: [size (2 bytes)][status (1 byte)]
    uint8_t batchHeader[BATCH_HEADER_SIZE];
    batchHeader[0] = length & 0xFF;        // Low byte of size
    batchHeader[1] = (length >> 8) & 0xFF; // High byte of size
    batchHeader[2] = BATCH_STATUS_VALID;

    uint32_t writeAddr = headPtr;

    // Write batch header
    writeAddr = writeWithWrap(writeAddr, batchHeader, BATCH_HEADER_SIZE);

    // Write batch data
    writeAddr = writeWithWrap(writeAddr, data, length);

    // Update head pointer and batch count
    headPtr = writeAddr;
    batchCount++;

    // Persist header
    if (!writeHeader()) {
        LOG_ERROR("FRAM: Failed to update header after write");
        return false;
    }

    LOG_DEBUG("FRAM: Wrote batch of %d bytes, count=%d", length, batchCount);
    return true;
}

bool FRAMBatchStorage::readBatch(uint8_t *buffer, uint16_t maxLength, uint16_t *actualLength)
{
    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    if (buffer == nullptr || actualLength == nullptr) {
        return false;
    }

    concurrency::LockGuard g(spiLock);

    // Refresh header
    if (!readHeader()) {
        LOG_ERROR("FRAM: Failed to read header");
        return false;
    }

    if (batchCount == 0) {
        LOG_DEBUG("FRAM: No batches to read");
        return false;
    }

    // Read batch header at tail position
    uint8_t batchHeader[BATCH_HEADER_SIZE];
    uint32_t readAddr = tailPtr;
    readWithWrap(readAddr, batchHeader, BATCH_HEADER_SIZE);

    uint16_t batchSize = batchHeader[0] | (batchHeader[1] << 8);
    uint8_t status = batchHeader[2];

    // Skip deleted batches (shouldn't normally happen, but safety check)
    if (status == BATCH_STATUS_DELETED || status == BATCH_STATUS_FREE) {
        LOG_WARN("FRAM: Found invalid batch at tail, status=%02X", status);
        return false;
    }

    if (batchSize > maxLength) {
        LOG_WARN("FRAM: Batch size %d exceeds buffer size %d", batchSize, maxLength);
        *actualLength = 0;
        return false;
    }

    // Read batch data (skip header)
    readAddr = wrapAddress(tailPtr + BATCH_HEADER_SIZE);
    readWithWrap(readAddr, buffer, batchSize);

    *actualLength = batchSize;
    LOG_DEBUG("FRAM: Read batch of %d bytes", batchSize);
    return true;
}

uint16_t FRAMBatchStorage::peekBatchSize()
{
    if (!initialized || batchCount == 0) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);

    uint8_t sizeBytes[2];
    readWithWrap(tailPtr, sizeBytes, 2);
    return sizeBytes[0] | (sizeBytes[1] << 8);
}

bool FRAMBatchStorage::deleteBatch()
{
    if (!initialized) {
        LOG_WARN("FRAM: Not initialized");
        return false;
    }

    concurrency::LockGuard g(spiLock);

    // Refresh header
    if (!readHeader()) {
        LOG_ERROR("FRAM: Failed to read header for delete");
        return false;
    }

    if (batchCount == 0) {
        LOG_DEBUG("FRAM: No batches to delete");
        return false;
    }

    // Read batch header to get size
    uint8_t batchHeader[BATCH_HEADER_SIZE];
    readWithWrap(tailPtr, batchHeader, BATCH_HEADER_SIZE);

    uint16_t batchSize = batchHeader[0] | (batchHeader[1] << 8);

    // Mark as deleted
    uint8_t deleteStatus = BATCH_STATUS_DELETED;
    uint32_t statusAddr = wrapAddress(tailPtr + 2);
    fram.write(statusAddr, &deleteStatus, 1);

    // Move tail pointer past this batch
    uint32_t totalSize = BATCH_HEADER_SIZE + batchSize;
    tailPtr = wrapAddress(tailPtr + totalSize);
    batchCount--;

    // Persist header
    if (!writeHeader()) {
        LOG_ERROR("FRAM: Failed to update header after delete");
        return false;
    }

    LOG_DEBUG("FRAM: Deleted batch of %d bytes, remaining=%d", batchSize, batchCount);
    return true;
}

uint8_t FRAMBatchStorage::getBatchCount()
{
    if (!initialized) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);
    readHeader(); // Refresh from FRAM
    return batchCount;
}

bool FRAMBatchStorage::hasBatches()
{
    return getBatchCount() > 0;
}

uint32_t FRAMBatchStorage::getAvailableSpace()
{
    if (!initialized) {
        return 0;
    }

    concurrency::LockGuard g(spiLock);
    readHeader();

    uint32_t dataSize = dataEndAddr - dataStartAddr;

    if (batchCount == 0) {
        return dataSize;
    }

    // Calculate used space in circular buffer
    if (headPtr >= tailPtr) {
        return dataSize - (headPtr - tailPtr);
    } else {
        return tailPtr - headPtr;
    }
}

bool FRAMBatchStorage::format()
{
    concurrency::LockGuard g(spiLock);

    // Initialize header with default values
    headPtr = dataStartAddr;
    tailPtr = dataStartAddr;
    batchCount = 0;

    return initHeader();
}

bool FRAMBatchStorage::getDeviceID(uint8_t *manufacturerID, uint16_t *productID)
{
    if (manufacturerID == nullptr || productID == nullptr) {
        return false;
    }

    concurrency::LockGuard g(spiLock);
    return fram.getDeviceID(manufacturerID, productID);
}

bool FRAMBatchStorage::enterSleep()
{
    if (!initialized) {
        return false;
    }

    concurrency::LockGuard g(spiLock);
    return fram.enterSleep();
}

bool FRAMBatchStorage::exitSleep()
{
    if (!initialized) {
        return false;
    }

    concurrency::LockGuard g(spiLock);
    return fram.exitSleep();
}

bool FRAMBatchStorage::readHeader()
{
    uint8_t header[FRAM_HEADER_SIZE];
    fram.read(0, header, FRAM_HEADER_SIZE);

    // Verify magic number
    uint16_t magic = header[FRAM_OFFSET_MAGIC] | (header[FRAM_OFFSET_MAGIC + 1] << 8);
    if (magic != FRAM_MAGIC_NUMBER) {
        LOG_DEBUG("FRAM: Invalid magic number %04X", magic);
        return false;
    }

    // Verify version
    if (header[FRAM_OFFSET_VERSION] != FRAM_VERSION) {
        LOG_WARN("FRAM: Version mismatch (found %d, expected %d)", header[FRAM_OFFSET_VERSION], FRAM_VERSION);
        return false;
    }

    // Read header values
    batchCount = header[FRAM_OFFSET_BATCH_COUNT];

    headPtr = header[FRAM_OFFSET_HEAD] | (header[FRAM_OFFSET_HEAD + 1] << 8) | (header[FRAM_OFFSET_HEAD + 2] << 16) |
              (header[FRAM_OFFSET_HEAD + 3] << 24);

    tailPtr = header[FRAM_OFFSET_TAIL] | (header[FRAM_OFFSET_TAIL + 1] << 8) | (header[FRAM_OFFSET_TAIL + 2] << 16) |
              (header[FRAM_OFFSET_TAIL + 3] << 24);

    // Validate pointers
    if (headPtr < dataStartAddr || headPtr >= dataEndAddr || tailPtr < dataStartAddr || tailPtr >= dataEndAddr) {
        LOG_WARN("FRAM: Invalid pointers (head=%lu, tail=%lu)", headPtr, tailPtr);
        return false;
    }

    return true;
}

bool FRAMBatchStorage::writeHeader()
{
    uint8_t header[FRAM_HEADER_SIZE] = {0};

    // Magic number
    header[FRAM_OFFSET_MAGIC] = FRAM_MAGIC_NUMBER & 0xFF;
    header[FRAM_OFFSET_MAGIC + 1] = (FRAM_MAGIC_NUMBER >> 8) & 0xFF;

    // Version
    header[FRAM_OFFSET_VERSION] = FRAM_VERSION;

    // Batch count
    header[FRAM_OFFSET_BATCH_COUNT] = batchCount;

    // Head pointer
    header[FRAM_OFFSET_HEAD] = headPtr & 0xFF;
    header[FRAM_OFFSET_HEAD + 1] = (headPtr >> 8) & 0xFF;
    header[FRAM_OFFSET_HEAD + 2] = (headPtr >> 16) & 0xFF;
    header[FRAM_OFFSET_HEAD + 3] = (headPtr >> 24) & 0xFF;

    // Tail pointer
    header[FRAM_OFFSET_TAIL] = tailPtr & 0xFF;
    header[FRAM_OFFSET_TAIL + 1] = (tailPtr >> 8) & 0xFF;
    header[FRAM_OFFSET_TAIL + 2] = (tailPtr >> 16) & 0xFF;
    header[FRAM_OFFSET_TAIL + 3] = (tailPtr >> 24) & 0xFF;

    // Flags (reserved, set to 0)
    header[FRAM_OFFSET_FLAGS] = 0;
    header[FRAM_OFFSET_FLAGS + 1] = 0;
    header[FRAM_OFFSET_FLAGS + 2] = 0;
    header[FRAM_OFFSET_FLAGS + 3] = 0;

    fram.writeEnable(true);
    bool success = fram.write(0, header, FRAM_HEADER_SIZE);
    fram.writeEnable(false);

    return success;
}

bool FRAMBatchStorage::initHeader()
{
    headPtr = dataStartAddr;
    tailPtr = dataStartAddr;
    batchCount = 0;

    bool success = writeHeader();
    if (success) {
        LOG_INFO("FRAM: Header initialized");
    }
    return success;
}

uint32_t FRAMBatchStorage::wrapAddress(uint32_t addr)
{
    uint32_t dataSize = dataEndAddr - dataStartAddr;
    if (addr >= dataEndAddr) {
        addr = dataStartAddr + ((addr - dataStartAddr) % dataSize);
    }
    return addr;
}

bool FRAMBatchStorage::hasSpaceFor(uint16_t size)
{
    // Calculate available space without acquiring lock (caller must hold spiLock)
    uint32_t dataSize = dataEndAddr - dataStartAddr;
    uint32_t available;

    if (batchCount == 0) {
        available = dataSize;
    } else if (headPtr >= tailPtr) {
        available = dataSize - (headPtr - tailPtr);
    } else {
        available = tailPtr - headPtr;
    }

    // Leave some margin to avoid head catching up to tail
    return available > (size + BATCH_HEADER_SIZE);
}

uint32_t FRAMBatchStorage::writeWithWrap(uint32_t addr, const uint8_t *data, uint16_t length)
{
    uint32_t dataSize = dataEndAddr - dataStartAddr;
    uint32_t bytesToEnd = dataEndAddr - addr;

    fram.writeEnable(true);

    if (length <= bytesToEnd) {
        // No wrap needed
        fram.write(addr, data, length);
        addr = wrapAddress(addr + length);
    } else {
        // Need to wrap around
        fram.write(addr, data, bytesToEnd);
        fram.write(dataStartAddr, data + bytesToEnd, length - bytesToEnd);
        addr = dataStartAddr + (length - bytesToEnd);
    }

    fram.writeEnable(false);
    return addr;
}

uint32_t FRAMBatchStorage::readWithWrap(uint32_t addr, uint8_t *buffer, uint16_t length)
{
    uint32_t bytesToEnd = dataEndAddr - addr;

    if (length <= bytesToEnd) {
        // No wrap needed
        fram.read(addr, buffer, length);
        addr = wrapAddress(addr + length);
    } else {
        // Need to wrap around
        fram.read(addr, buffer, bytesToEnd);
        fram.read(dataStartAddr, buffer + bytesToEnd, length - bytesToEnd);
        addr = dataStartAddr + (length - bytesToEnd);
    }

    return addr;
}

#endif // __has_include(<Adafruit_FRAM_SPI.h>)
