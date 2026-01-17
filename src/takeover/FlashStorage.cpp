/**
 * @file FlashStorage.cpp
 * @brief Flash memory storage backend implementation
 *
 * Follows NASA's 10 Rules of Safe Code.
 */

#include "FlashStorage.h"

/**
 * @brief Assertion macro - halts on failure (NASA Rule 5)
 */
#if FM25V02A_DEBUG
#define FLASH_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            Serial.printf("FLASH ASSERT FAILED: %s:%d - %s\n", __FILE__, __LINE__, #cond); \
            Serial.flush(); \
            while (true) { } \
        } \
    } while (false)
#else
#define FLASH_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            while (true) { } \
        } \
    } while (false)
#endif

/**
 * @brief CRC16 polynomial (CRC-16-CCITT)
 */
#define FLASH_CRC16_POLY 0x1021U
#define FLASH_CRC16_INIT 0xFFFFU

/**
 * @brief Typical flash endurance (100,000 cycles)
 */
#define FLASH_TYPICAL_ENDURANCE 100000U

FlashStorage::FlashStorage(SPIClass *spi, uint8_t csPin, uint32_t spiSpeed,
                           uint32_t baseAddress, uint32_t size)
    : m_spi(spi)
    , m_spiSettings(spiSpeed, MSBFIRST, SPI_MODE0)
    , m_csPin(csPin)
    , m_baseAddress(baseAddress)
    , m_size(size)
    , m_initialized(false)
    , m_errorCount(0U)
    , m_writeCount(0U)
    , m_maxEraseCount(0U)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(spi != nullptr);
    FLASH_ASSERT(size > 0U);
    FLASH_ASSERT((baseAddress + size) <= FLASH_MAX_ADDRESS);

    /* Configure CS pin */
    pinMode(m_csPin, OUTPUT);
    digitalWrite(m_csPin, HIGH);
}

StorageError FlashStorage::init(void)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(m_spi != nullptr);
    FLASH_ASSERT(!m_initialized);

    if (m_spi == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    /* Release from power-down if needed */
    beginTransaction();
    m_spi->transfer(OPCODE_RELEASE_POWER_DOWN);
    endTransaction();
    delayMicroseconds(50U); /* tRES1 recovery time */

    /* Verify device responds */
    uint8_t manufacturerId = 0U;
    uint16_t deviceId = 0U;
    StorageError err = readDeviceId(&manufacturerId, &deviceId);
    if (err != STORAGE_OK) {
        m_errorCount++;
        return err;
    }

    /* Verify valid manufacturer ID (common flash ICs) */
    /* Winbond=0xEF, Macronix=0xC2, Spansion=0x01, Micron=0x20 */
    if ((manufacturerId != 0xEFU) && (manufacturerId != 0xC2U) &&
        (manufacturerId != 0x01U) && (manufacturerId != 0x20U)) {
        m_errorCount++;
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* Scan for highest erase count across sectors */
    const uint32_t numSectors = m_size / FLASH_SECTOR_SIZE;
    for (uint32_t i = 0U; i < numSectors; i++) {
        FlashWearInfo info;
        uint32_t sectorAddr = m_baseAddress + (i * FLASH_SECTOR_SIZE);
        if (readWearInfo(sectorAddr, &info) == STORAGE_OK) {
            if (info.eraseCount > m_maxEraseCount) {
                m_maxEraseCount = info.eraseCount;
            }
        }
    }

    m_initialized = true;
    return STORAGE_OK;
}

StorageError FlashStorage::read(uint32_t address, uint8_t *buffer, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(buffer != nullptr);
    FLASH_ASSERT(size <= FLASH_MAX_TRANSFER_SIZE);

    if (buffer == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    if (!m_initialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* Validate address range */
    const uint32_t absAddress = m_baseAddress + address;
    if ((absAddress + size) > (m_baseAddress + m_size)) {
        return STORAGE_ERR_INVALID_ADDRESS;
    }

    /* Read data using fast read command */
    beginTransaction();
    m_spi->transfer(OPCODE_FAST_READ);
    m_spi->transfer(static_cast<uint8_t>((absAddress >> 16U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>((absAddress >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(absAddress & 0xFFU));
    m_spi->transfer(0x00U); /* Dummy byte for fast read */

    /* NASA Rule 2: Bounded loop */
    for (uint16_t i = 0U; i < size; i++) {
        buffer[i] = m_spi->transfer(0x00U);
    }
    endTransaction();

    return STORAGE_OK;
}

StorageError FlashStorage::write(uint32_t address, const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(data != nullptr);
    FLASH_ASSERT(size > 0U);

    if (data == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    if (!m_initialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* Validate address range */
    const uint32_t absAddress = m_baseAddress + address;
    if ((absAddress + size) > (m_baseAddress + m_size)) {
        return STORAGE_ERR_INVALID_ADDRESS;
    }

    /* Write data in page-sized chunks */
    uint32_t remaining = size;
    uint32_t offset = 0U;

    /* NASA Rule 2: Bounded loop (max iterations = size/1 = 256 for max transfer) */
    while (remaining > 0U) {
        /* Calculate bytes to write in this page */
        const uint32_t pageOffset = (absAddress + offset) % FLASH_PAGE_SIZE;
        uint32_t bytesThisPage = FLASH_PAGE_SIZE - pageOffset;
        if (bytesThisPage > remaining) {
            bytesThisPage = remaining;
        }

        /* Write the page */
        StorageError err = writePage(absAddress + offset, data + offset,
                                     static_cast<uint16_t>(bytesThisPage));
        if (err != STORAGE_OK) {
            m_errorCount++;
            return err;
        }

        offset += bytesThisPage;
        remaining -= bytesThisPage;
    }

    m_writeCount++;
    return STORAGE_OK;
}

StorageError FlashStorage::erase(uint32_t address, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(m_initialized);
    FLASH_ASSERT(size > 0U);

    if (!m_initialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* Validate address range */
    const uint32_t absAddress = m_baseAddress + address;
    if ((absAddress + size) > (m_baseAddress + m_size)) {
        return STORAGE_ERR_INVALID_ADDRESS;
    }

    /* Align to sector boundaries */
    const uint32_t startSector = (absAddress / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    const uint32_t endAddress = absAddress + size;
    const uint32_t endSector = ((endAddress + FLASH_SECTOR_SIZE - 1U) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

    /* Erase each sector */
    for (uint32_t sector = startSector; sector < endSector; sector += FLASH_SECTOR_SIZE) {
        StorageError err = eraseSector(sector);
        if (err != STORAGE_OK) {
            m_errorCount++;
            return err;
        }

        /* Update wear tracking */
        err = updateWearInfo(sector);
        if (err != STORAGE_OK) {
            /* Non-fatal - continue even if wear tracking fails */
            m_errorCount++;
        }
    }

    return STORAGE_OK;
}

StorageType FlashStorage::getType(void) const
{
    return STORAGE_TYPE_FLASH;
}

uint32_t FlashStorage::getCapacity(void) const
{
    return m_size;
}

bool FlashStorage::isReady(void) const
{
    return m_initialized;
}

StorageError FlashStorage::getHealth(StorageHealth *health) const
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(health != nullptr);

    if (health == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    health->initialized = m_initialized;
    health->healthy = m_initialized && (m_maxEraseCount < FLASH_TYPICAL_ENDURANCE);
    health->degraded = m_maxEraseCount > (FLASH_TYPICAL_ENDURANCE / 2U);
    health->errorCount = m_errorCount;
    health->writeCount = m_writeCount;

    /* Calculate health percentage based on remaining endurance */
    if (m_maxEraseCount >= FLASH_TYPICAL_ENDURANCE) {
        health->healthPercent = 0U;
    } else {
        const uint32_t remaining = FLASH_TYPICAL_ENDURANCE - m_maxEraseCount;
        health->healthPercent = static_cast<uint8_t>((remaining * 100U) / FLASH_TYPICAL_ENDURANCE);
    }

    return STORAGE_OK;
}

StorageError FlashStorage::readDeviceId(uint8_t *manufacturerId, uint16_t *deviceId)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(manufacturerId != nullptr);
    FLASH_ASSERT(deviceId != nullptr);

    if ((manufacturerId == nullptr) || (deviceId == nullptr)) {
        return STORAGE_ERR_NULL_POINTER;
    }

    beginTransaction();
    m_spi->transfer(OPCODE_READ_ID);
    *manufacturerId = m_spi->transfer(0x00U);
    uint8_t memType = m_spi->transfer(0x00U);
    uint8_t capacity = m_spi->transfer(0x00U);
    endTransaction();

    *deviceId = (static_cast<uint16_t>(memType) << 8U) | capacity;

    return STORAGE_OK;
}

uint32_t FlashStorage::getRemainingCycles(void) const
{
    if (m_maxEraseCount >= FLASH_TYPICAL_ENDURANCE) {
        return 0U;
    }
    return FLASH_TYPICAL_ENDURANCE - m_maxEraseCount;
}

void FlashStorage::beginTransaction(void)
{
    m_spi->beginTransaction(m_spiSettings);
    digitalWrite(m_csPin, LOW);
}

void FlashStorage::endTransaction(void)
{
    digitalWrite(m_csPin, HIGH);
    m_spi->endTransaction();
}

StorageError FlashStorage::writeEnable(void)
{
    beginTransaction();
    m_spi->transfer(OPCODE_WRITE_ENABLE);
    endTransaction();

    /* Verify WEL bit is set */
    const uint8_t status = readStatus();
    if ((status & FLASH_STATUS_WEL) == 0U) {
        return STORAGE_ERR_WRITE_FAILED;
    }

    return STORAGE_OK;
}

StorageError FlashStorage::waitReady(uint32_t timeoutUs)
{
    const uint32_t startTime = micros();

    /* NASA Rule 2: Bounded loop with timeout */
    while ((micros() - startTime) < timeoutUs) {
        const uint8_t status = readStatus();
        if ((status & FLASH_STATUS_BUSY) == 0U) {
            return STORAGE_OK;
        }
        delayMicroseconds(10U);
    }

    return STORAGE_ERR_DEVICE_BUSY;
}

uint8_t FlashStorage::readStatus(void)
{
    beginTransaction();
    m_spi->transfer(OPCODE_READ_STATUS);
    uint8_t status = m_spi->transfer(0x00U);
    endTransaction();
    return status;
}

StorageError FlashStorage::writePage(uint32_t address, const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(data != nullptr);
    FLASH_ASSERT(size <= FLASH_PAGE_SIZE);

    /* Enable writes */
    StorageError err = writeEnable();
    if (err != STORAGE_OK) {
        return err;
    }

    /* Send page program command */
    beginTransaction();
    m_spi->transfer(OPCODE_PAGE_PROGRAM);
    m_spi->transfer(static_cast<uint8_t>((address >> 16U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>((address >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(address & 0xFFU));

    /* NASA Rule 2: Bounded loop */
    for (uint16_t i = 0U; i < size; i++) {
        m_spi->transfer(data[i]);
    }
    endTransaction();

    /* Wait for programming to complete */
    err = waitReady(FLASH_PAGE_PROGRAM_TIMEOUT_US);
    if (err != STORAGE_OK) {
        return STORAGE_ERR_WRITE_FAILED;
    }

    return STORAGE_OK;
}

StorageError FlashStorage::eraseSector(uint32_t sectorAddress)
{
    /* Enable writes */
    StorageError err = writeEnable();
    if (err != STORAGE_OK) {
        return err;
    }

    /* Send sector erase command */
    beginTransaction();
    m_spi->transfer(OPCODE_SECTOR_ERASE);
    m_spi->transfer(static_cast<uint8_t>((sectorAddress >> 16U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>((sectorAddress >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(sectorAddress & 0xFFU));
    endTransaction();

    /* Wait for erase to complete */
    err = waitReady(FLASH_SECTOR_ERASE_TIMEOUT_US);
    if (err != STORAGE_OK) {
        return STORAGE_ERR_ERASE_FAILED;
    }

    return STORAGE_OK;
}

StorageError FlashStorage::updateWearInfo(uint32_t sectorAddress)
{
    FlashWearInfo info;

    /* Read existing wear info */
    StorageError err = readWearInfo(sectorAddress, &info);
    if (err != STORAGE_OK) {
        /* Initialize new wear info */
        info.magic = FLASH_WEAR_MAGIC;
        info.eraseCount = 1U;
    } else {
        info.eraseCount++;
    }

    /* Update max erase count tracking */
    if (info.eraseCount > m_maxEraseCount) {
        m_maxEraseCount = info.eraseCount;
    }

    /* Calculate CRC */
    uint8_t data[8U];
    data[0U] = static_cast<uint8_t>((info.magic >> 24U) & 0xFFU);
    data[1U] = static_cast<uint8_t>((info.magic >> 16U) & 0xFFU);
    data[2U] = static_cast<uint8_t>((info.magic >> 8U) & 0xFFU);
    data[3U] = static_cast<uint8_t>(info.magic & 0xFFU);
    data[4U] = static_cast<uint8_t>((info.eraseCount >> 24U) & 0xFFU);
    data[5U] = static_cast<uint8_t>((info.eraseCount >> 16U) & 0xFFU);
    data[6U] = static_cast<uint8_t>((info.eraseCount >> 8U) & 0xFFU);
    data[7U] = static_cast<uint8_t>(info.eraseCount & 0xFFU);
    info.crc = calculateCRC16(data, 8U);

    /* Write wear info at end of sector */
    const uint32_t wearAddr = sectorAddress + FLASH_SECTOR_SIZE - FLASH_WEAR_INFO_SIZE;

    uint8_t wearBytes[FLASH_WEAR_INFO_SIZE];
    for (uint8_t i = 0U; i < 8U; i++) {
        wearBytes[i] = data[i];
    }
    wearBytes[8U] = static_cast<uint8_t>((info.crc >> 8U) & 0xFFU);
    wearBytes[9U] = static_cast<uint8_t>(info.crc & 0xFFU);

    return writePage(wearAddr, wearBytes, FLASH_WEAR_INFO_SIZE);
}

StorageError FlashStorage::readWearInfo(uint32_t sectorAddress, FlashWearInfo *info)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(info != nullptr);

    if (info == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    /* Read wear info from end of sector */
    const uint32_t wearAddr = sectorAddress + FLASH_SECTOR_SIZE - FLASH_WEAR_INFO_SIZE;
    uint8_t wearBytes[FLASH_WEAR_INFO_SIZE];

    /* Direct read without address translation */
    beginTransaction();
    m_spi->transfer(OPCODE_FAST_READ);
    m_spi->transfer(static_cast<uint8_t>((wearAddr >> 16U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>((wearAddr >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(wearAddr & 0xFFU));
    m_spi->transfer(0x00U); /* Dummy byte */

    for (uint8_t i = 0U; i < FLASH_WEAR_INFO_SIZE; i++) {
        wearBytes[i] = m_spi->transfer(0x00U);
    }
    endTransaction();

    /* Parse wear info */
    info->magic = (static_cast<uint32_t>(wearBytes[0U]) << 24U) |
                  (static_cast<uint32_t>(wearBytes[1U]) << 16U) |
                  (static_cast<uint32_t>(wearBytes[2U]) << 8U) |
                   static_cast<uint32_t>(wearBytes[3U]);

    info->eraseCount = (static_cast<uint32_t>(wearBytes[4U]) << 24U) |
                       (static_cast<uint32_t>(wearBytes[5U]) << 16U) |
                       (static_cast<uint32_t>(wearBytes[6U]) << 8U) |
                        static_cast<uint32_t>(wearBytes[7U]);

    info->crc = (static_cast<uint16_t>(wearBytes[8U]) << 8U) |
                 static_cast<uint16_t>(wearBytes[9U]);

    /* Validate magic and CRC */
    if (info->magic != FLASH_WEAR_MAGIC) {
        return STORAGE_ERR_READ_FAILED;
    }

    const uint16_t calculatedCrc = calculateCRC16(wearBytes, 8U);
    if (calculatedCrc != info->crc) {
        return STORAGE_ERR_VERIFY_FAILED;
    }

    return STORAGE_OK;
}

uint16_t FlashStorage::calculateCRC16(const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FLASH_ASSERT(data != nullptr);

    if ((data == nullptr) || (size == 0U)) {
        return 0U;
    }

    uint16_t crc = FLASH_CRC16_INIT;

    /* NASA Rule 2: Bounded loop */
    for (uint16_t i = 0U; i < size; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8U;

        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (crc << 1U) ^ FLASH_CRC16_POLY;
            } else {
                crc = crc << 1U;
            }
        }
    }

    return crc;
}
