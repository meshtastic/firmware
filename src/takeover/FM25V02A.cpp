/**
 * @file FM25V02A.cpp
 * @brief FM25V02A 256-Kbit Serial SPI F-RAM Driver Implementation
 *
 * Implementation follows NASA's 10 Rules of Safe Code.
 * All functions include parameter validation and assertions.
 */

#include "FM25V02A.h"

/**
 * @brief Compile-time assertions for constants
 * Ensures configuration is valid at compile time.
 */
static_assert(FM25V02A_MAX_TRANSFER_SIZE == 256U,
              "FM25V02A_MAX_TRANSFER_SIZE must be 256 bytes");
static_assert(FM25V02A_MEMORY_SIZE == 32768U,
              "FM25V02A_MEMORY_SIZE must be 32KB (256Kbit)");
static_assert(FM25V02A_MAX_ADDRESS == 32767U,
              "FM25V02A_MAX_ADDRESS must be 0x7FFF");
static_assert(FM25V02A_ADDRESS_BYTES == 2U,
              "FM25V02A requires 2-byte addresses");

/**
 * @brief Assertion macro - halts on failure (NASA Rule 5)
 *
 * In production, this triggers an infinite loop to halt execution.
 * In debug mode, outputs file and line number before halting.
 */
#if FM25V02A_DEBUG
#define FM25V02A_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            Serial.printf("ASSERT FAILED: %s:%d - %s\n", __FILE__, __LINE__, #cond); \
            Serial.flush(); \
            while (true) { \
                /* Halt execution - NASA Rule 5 */ \
            } \
        } \
    } while (false)
#else
#define FM25V02A_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            while (true) { \
                /* Halt execution - NASA Rule 5 */ \
            } \
        } \
    } while (false)
#endif

/**
 * @brief Thread safety macros
 */
#if FM25V02A_THREAD_SAFE
#define FM25V02A_LOCK() \
    do { xSemaphoreTake(m_mutex, portMAX_DELAY); } while (false)
#define FM25V02A_UNLOCK() \
    do { xSemaphoreGive(m_mutex); } while (false)
#else
#define FM25V02A_LOCK() do { } while (false)
#define FM25V02A_UNLOCK() do { } while (false)
#endif

/**
 * @brief Static error strings for getErrorString()
 */
static const char *const ERROR_STRINGS[] = {
    "OK",                       /* FM25V02A_OK */
    "Null pointer",             /* FM25V02A_ERR_NULL_POINTER */
    "Invalid address",          /* FM25V02A_ERR_INVALID_ADDRESS */
    "Invalid size",             /* FM25V02A_ERR_INVALID_SIZE */
    "Address overflow",         /* FM25V02A_ERR_ADDRESS_OVERFLOW */
    "Not initialized",          /* FM25V02A_ERR_NOT_INITIALIZED */
    "Device ID mismatch",       /* FM25V02A_ERR_DEVICE_ID */
    "Write enable failed",      /* FM25V02A_ERR_WRITE_ENABLE */
    "CRC mismatch",             /* FM25V02A_ERR_CRC_MISMATCH */
    "SPI bus null",             /* FM25V02A_ERR_SPI_NULL */
    "Assertion failed",         /* FM25V02A_ERR_ASSERTION */
    "Device asleep",            /* FM25V02A_ERR_ASLEEP */
    "Write protected"           /* FM25V02A_ERR_WRITE_PROTECTED */
};

static const uint8_t ERROR_STRING_COUNT = 13U;

FM25V02A::FM25V02A(SPIClass *spi, uint8_t csPin, uint32_t spiSpeed)
    : m_spi(spi)
    , m_spiSettings(spiSpeed, MSBFIRST, SPI_MODE0)
    , m_csPin(csPin)
    , m_state{false, false, 0U}
    , m_errorCallback(nullptr)
    , m_errorContext(nullptr)
#if FM25V02A_THREAD_SAFE
    , m_mutex(nullptr)
#endif
{
    /* NASA Rule 5: Assertions for constructor parameters */
    FM25V02A_ASSERT(spi != nullptr);
    FM25V02A_ASSERT(spiSpeed <= FM25V02A_MAX_SPI_SPEED);

    /* Configure CS pin as output, deasserted (high) */
    pinMode(m_csPin, OUTPUT);
    digitalWrite(m_csPin, HIGH);

#if FM25V02A_THREAD_SAFE
    m_mutex = xSemaphoreCreateMutex();
    FM25V02A_ASSERT(m_mutex != nullptr);
#endif
}

FM25V02A_Error FM25V02A::init(void)
{
    /* NASA Rule 5: Assertion for preconditions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(!m_state.initialized);

    FM25V02A_LOCK();

    /* NASA Rule 7: Validate SPI bus */
    if (m_spi == nullptr) {
        reportError(FM25V02A_ERR_SPI_NULL, 0U);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_SPI_NULL;
    }

    /* Reset state */
    m_state.initialized = false;
    m_state.asleep = false;
    m_state.status = 0U;

    /* Read and verify device ID */
    uint32_t manufacturerId = 0U;
    uint16_t productId = 0U;
    FM25V02A_Error err = readDeviceId(&manufacturerId, &productId);

    /* NASA Rule 7: Check return value */
    if (err != FM25V02A_OK) {
        reportError(err, 0U);
        FM25V02A_UNLOCK();
        return err;
    }

    /* Validate manufacturer ID (Cypress/Infineon) */
    const uint8_t mfrByte1 = static_cast<uint8_t>((manufacturerId >> 8U) & 0xFFU);
    const uint8_t mfrByte2 = static_cast<uint8_t>(manufacturerId & 0xFFU);

    if ((mfrByte1 != FM25V02A_MANUFACTURER_ID_BYTE1) ||
        (mfrByte2 != FM25V02A_MANUFACTURER_ID_BYTE2)) {
        reportError(FM25V02A_ERR_DEVICE_ID, 0U);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_DEVICE_ID;
    }

    /* Verify product ID (upper byte indicates FM25V02A) */
    const uint8_t densityCode = static_cast<uint8_t>((productId >> 8U) & 0x1FU);
    /* NASA Rule 5: Third assertion - validate expected density */
    FM25V02A_ASSERT(densityCode == 0x02U); /* 256Kbit density */

    if (densityCode != 0x02U) {
        reportError(FM25V02A_ERR_DEVICE_ID, 0U);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_DEVICE_ID;
    }

    /* Read initial status for protection cache */
    uint8_t status = 0U;
    err = readStatus(&status);
    if (err != FM25V02A_OK) {
        reportError(err, 0U);
        FM25V02A_UNLOCK();
        return err;
    }

    m_state.initialized = true;
    FM25V02A_UNLOCK();
    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::read(uint16_t address, uint8_t *buffer, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(buffer != nullptr);
    FM25V02A_ASSERT(size <= FM25V02A_MAX_TRANSFER_SIZE);

    /* NASA Rule 7: Parameter validation */
    if (buffer == nullptr) {
        reportError(FM25V02A_ERR_NULL_POINTER, address);
        return FM25V02A_ERR_NULL_POINTER;
    }

    FM25V02A_LOCK();

    if (!m_state.initialized) {
        reportError(FM25V02A_ERR_NOT_INITIALIZED, address);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (m_state.asleep) {
        reportError(FM25V02A_ERR_ASLEEP, address);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_ASLEEP;
    }

    FM25V02A_Error err = validateAddressAndSize(address, size);
    if (err != FM25V02A_OK) {
        reportError(err, address);
        FM25V02A_UNLOCK();
        return err;
    }

    /* Perform read operation */
    beginTransaction();
    m_spi->transfer(OPCODE_READ);
    m_spi->transfer(static_cast<uint8_t>((address >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(address & 0xFFU));

    /* NASA Rule 2: Bounded loop */
    for (uint16_t i = 0U; i < size; i++) {
        buffer[i] = m_spi->transfer(0x00U);
    }

    endTransaction();
    FM25V02A_UNLOCK();
    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::write(uint16_t address, const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(data != nullptr);
    FM25V02A_ASSERT(size <= FM25V02A_MAX_TRANSFER_SIZE);

    /* NASA Rule 7: Parameter validation */
    if (data == nullptr) {
        reportError(FM25V02A_ERR_NULL_POINTER, address);
        return FM25V02A_ERR_NULL_POINTER;
    }

    FM25V02A_LOCK();

    if (!m_state.initialized) {
        reportError(FM25V02A_ERR_NOT_INITIALIZED, address);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (m_state.asleep) {
        reportError(FM25V02A_ERR_ASLEEP, address);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_ASLEEP;
    }

    FM25V02A_Error err = validateAddressAndSize(address, size);
    if (err != FM25V02A_OK) {
        reportError(err, address);
        FM25V02A_UNLOCK();
        return err;
    }

    /* Refresh protection status from hardware before checking (fix stale cache) */
    err = refreshProtectionStatus();
    if (err != FM25V02A_OK) {
        FM25V02A_UNLOCK();
        return err;
    }

    /* Check write protection with fresh status */
    if (isWriteProtected(address, size)) {
        reportError(FM25V02A_ERR_WRITE_PROTECTED, address);
        FM25V02A_UNLOCK();
        return FM25V02A_ERR_WRITE_PROTECTED;
    }

    /* Enable writes */
    err = writeEnable();
    if (err != FM25V02A_OK) {
        FM25V02A_UNLOCK();
        return err;
    }

    /* Perform write operation */
    beginTransaction();
    m_spi->transfer(OPCODE_WRITE);
    m_spi->transfer(static_cast<uint8_t>((address >> 8U) & 0xFFU));
    m_spi->transfer(static_cast<uint8_t>(address & 0xFFU));

    /* NASA Rule 2: Bounded loop */
    for (uint16_t i = 0U; i < size; i++) {
        m_spi->transfer(data[i]);
    }

    endTransaction();

    /* Write disable happens automatically on CS high */
    FM25V02A_UNLOCK();
    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::readWithCRC(uint16_t address, uint8_t *buffer, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(buffer != nullptr);
    FM25V02A_ASSERT(size > 0U);

    /* NASA Rule 7: Validate parameters */
    if (buffer == nullptr) {
        reportError(FM25V02A_ERR_NULL_POINTER, address);
        return FM25V02A_ERR_NULL_POINTER;
    }

    /* Check that data + CRC fits in memory */
    const uint16_t totalSize = size + 2U;
    if ((static_cast<uint32_t>(address) + totalSize) > FM25V02A_MEMORY_SIZE) {
        reportError(FM25V02A_ERR_ADDRESS_OVERFLOW, address);
        return FM25V02A_ERR_ADDRESS_OVERFLOW;
    }

    /* Read data */
    FM25V02A_Error err = read(address, buffer, size);
    if (err != FM25V02A_OK) {
        return err;
    }

    /* Read stored CRC */
    uint8_t crcBytes[2U];
    err = read(address + size, crcBytes, 2U);
    if (err != FM25V02A_OK) {
        return err;
    }

    const uint16_t storedCRC = (static_cast<uint16_t>(crcBytes[0U]) << 8U) |
                                static_cast<uint16_t>(crcBytes[1U]);

    /* Calculate and compare CRC */
    const uint16_t calculatedCRC = calculateCRC16(buffer, size);

    if (calculatedCRC != storedCRC) {
        reportError(FM25V02A_ERR_CRC_MISMATCH, address);
        return FM25V02A_ERR_CRC_MISMATCH;
    }

    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::writeWithCRC(uint16_t address, const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(data != nullptr);
    FM25V02A_ASSERT(size > 0U);

    /* NASA Rule 7: Validate parameters */
    if (data == nullptr) {
        reportError(FM25V02A_ERR_NULL_POINTER, address);
        return FM25V02A_ERR_NULL_POINTER;
    }

    /* Check that data + CRC fits in memory */
    const uint16_t totalSize = size + 2U;
    if ((static_cast<uint32_t>(address) + totalSize) > FM25V02A_MEMORY_SIZE) {
        reportError(FM25V02A_ERR_ADDRESS_OVERFLOW, address);
        return FM25V02A_ERR_ADDRESS_OVERFLOW;
    }

    /* Write data */
    FM25V02A_Error err = write(address, data, size);
    if (err != FM25V02A_OK) {
        return err;
    }

    /* Calculate and write CRC */
    const uint16_t crc = calculateCRC16(data, size);
    uint8_t crcBytes[2U];
    crcBytes[0U] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    crcBytes[1U] = static_cast<uint8_t>(crc & 0xFFU);

    err = write(address + size, crcBytes, 2U);
    return err;
}

FM25V02A_Error FM25V02A::readByte(uint16_t address, uint8_t *value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(value != nullptr);
    FM25V02A_ASSERT(address <= FM25V02A_MAX_ADDRESS);

    return read(address, value, 1U);
}

FM25V02A_Error FM25V02A::writeByte(uint16_t address, uint8_t value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(address <= FM25V02A_MAX_ADDRESS);
    FM25V02A_ASSERT(m_state.initialized);

    return write(address, &value, 1U);
}

FM25V02A_Error FM25V02A::readUint16(uint16_t address, uint16_t *value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(value != nullptr);
    FM25V02A_ASSERT(address <= (FM25V02A_MAX_ADDRESS - 1U));

    uint8_t buffer[2U];
    FM25V02A_Error err = read(address, buffer, 2U);

    if (err == FM25V02A_OK) {
        *value = (static_cast<uint16_t>(buffer[0U]) << 8U) |
                  static_cast<uint16_t>(buffer[1U]);
    }

    return err;
}

FM25V02A_Error FM25V02A::writeUint16(uint16_t address, uint16_t value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(address <= (FM25V02A_MAX_ADDRESS - 1U));
    FM25V02A_ASSERT(m_state.initialized);

    uint8_t buffer[2U];
    buffer[0U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    buffer[1U] = static_cast<uint8_t>(value & 0xFFU);

    return write(address, buffer, 2U);
}

FM25V02A_Error FM25V02A::readUint32(uint16_t address, uint32_t *value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(value != nullptr);
    FM25V02A_ASSERT(address <= (FM25V02A_MAX_ADDRESS - 3U));

    uint8_t buffer[4U];
    FM25V02A_Error err = read(address, buffer, 4U);

    if (err == FM25V02A_OK) {
        *value = (static_cast<uint32_t>(buffer[0U]) << 24U) |
                 (static_cast<uint32_t>(buffer[1U]) << 16U) |
                 (static_cast<uint32_t>(buffer[2U]) << 8U) |
                  static_cast<uint32_t>(buffer[3U]);
    }

    return err;
}

FM25V02A_Error FM25V02A::writeUint32(uint16_t address, uint32_t value)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(address <= (FM25V02A_MAX_ADDRESS - 3U));
    FM25V02A_ASSERT(m_state.initialized);

    uint8_t buffer[4U];
    buffer[0U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    buffer[1U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    buffer[2U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    buffer[3U] = static_cast<uint8_t>(value & 0xFFU);

    return write(address, buffer, 4U);
}

FM25V02A_Error FM25V02A::sleep(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(m_state.initialized);

    if (!m_state.initialized) {
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (m_state.asleep) {
        return FM25V02A_OK; /* Already asleep */
    }

    beginTransaction();
    m_spi->transfer(OPCODE_SLEEP);
    endTransaction();

    m_state.asleep = true;
    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::wake(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(m_state.initialized);

    if (!m_state.initialized) {
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (!m_state.asleep) {
        return FM25V02A_OK; /* Already awake */
    }

    /*
     * Wake sequence per FM25V02A datasheet section 6.8:
     * 1. Assert CS (low) - this initiates wake
     * 2. Wait tREC (400us max recovery time)
     * 3. Deassert CS (high)
     * The device will wake on the CS falling edge and be ready
     * after tREC has elapsed.
     */
    digitalWrite(m_csPin, LOW);
    delayMicroseconds(FM25V02A_WAKE_DELAY_US);
    digitalWrite(m_csPin, HIGH);

    m_state.asleep = false;
    return FM25V02A_OK;
}

bool FM25V02A::isAsleep(void) const
{
    return m_state.asleep;
}

FM25V02A_Error FM25V02A::readStatus(uint8_t *status)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(status != nullptr);
    FM25V02A_ASSERT(m_spi != nullptr);

    if (status == nullptr) {
        return FM25V02A_ERR_NULL_POINTER;
    }

    if (m_state.asleep) {
        return FM25V02A_ERR_ASLEEP;
    }

    beginTransaction();
    m_spi->transfer(OPCODE_RDSR);
    *status = m_spi->transfer(0x00U);
    endTransaction();

    m_state.status = *status;
    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::setProtection(FM25V02A_Protection protection)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_state.initialized);
    FM25V02A_ASSERT(protection <= FM25V02A_PROTECT_ALL);

    if (!m_state.initialized) {
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (m_state.asleep) {
        return FM25V02A_ERR_ASLEEP;
    }

    /* Enable writes to modify status register */
    FM25V02A_Error err = writeEnable();
    if (err != FM25V02A_OK) {
        return err;
    }

    /* Read current status to preserve other bits */
    uint8_t status = 0U;
    err = readStatus(&status);
    if (err != FM25V02A_OK) {
        return err;
    }

    /* Clear BP bits and set new protection level */
    status = (status & ~0x0CU) | static_cast<uint8_t>(protection);

    /* Write status register */
    beginTransaction();
    m_spi->transfer(OPCODE_WRSR);
    m_spi->transfer(status);
    endTransaction();

    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::getProtection(FM25V02A_Protection *protection)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(protection != nullptr);
    FM25V02A_ASSERT(m_state.initialized);

    if (protection == nullptr) {
        return FM25V02A_ERR_NULL_POINTER;
    }

    uint8_t status = 0U;
    FM25V02A_Error err = readStatus(&status);

    if (err == FM25V02A_OK) {
        *protection = static_cast<FM25V02A_Protection>(status & 0x0CU);
    }

    return err;
}

FM25V02A_Error FM25V02A::readDeviceId(uint32_t *manufacturerId, uint16_t *productId)
{
    /* NASA Rule 5: Assertions - at least one output must be valid */
    FM25V02A_ASSERT((manufacturerId != nullptr) || (productId != nullptr));
    FM25V02A_ASSERT(m_spi != nullptr);

    if (m_state.asleep) {
        return FM25V02A_ERR_ASLEEP;
    }

    uint8_t idBuffer[9U]; /* 7 manufacturer + 2 product */

    beginTransaction();
    m_spi->transfer(OPCODE_RDID);

    /* NASA Rule 2: Bounded loop */
    for (uint8_t i = 0U; i < 9U; i++) {
        idBuffer[i] = m_spi->transfer(0x00U);
    }

    endTransaction();

    /* Parse manufacturer ID (bytes 0-6, continuation codes + ID) */
    if (manufacturerId != nullptr) {
        *manufacturerId = (static_cast<uint32_t>(idBuffer[5U]) << 8U) |
                           static_cast<uint32_t>(idBuffer[6U]);
    }

    /* Parse product ID (bytes 7-8) */
    if (productId != nullptr) {
        *productId = (static_cast<uint16_t>(idBuffer[7U]) << 8U) |
                      static_cast<uint16_t>(idBuffer[8U]);
    }

    return FM25V02A_OK;
}

bool FM25V02A::isInitialized(void) const
{
    return m_state.initialized;
}

void FM25V02A::setErrorCallback(FM25V02A_ErrorCallback callback, void *context)
{
    m_errorCallback = callback;
    m_errorContext = context;
}

uint16_t FM25V02A::calculateCRC16(const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(data != nullptr);
    FM25V02A_ASSERT(size <= FM25V02A_MAX_TRANSFER_SIZE);

    if ((data == nullptr) || (size == 0U)) {
        return 0U;
    }

    /*
     * NASA Rule 2: Bound loop even for external callers
     * Clamp size to max transfer size to prevent unbounded loops
     * when called externally with large size values.
     */
    const uint16_t clampedSize = (size > FM25V02A_MAX_TRANSFER_SIZE) ?
                                  FM25V02A_MAX_TRANSFER_SIZE : size;

    uint16_t crc = FM25V02A_CRC16_INIT;

    /* NASA Rule 2: Bounded loop (max 256 iterations) */
    for (uint16_t i = 0U; i < clampedSize; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8U;

        /* Process each bit - bounded to 8 iterations */
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (crc << 1U) ^ FM25V02A_CRC16_POLY;
            } else {
                crc = crc << 1U;
            }
        }
    }

    return crc;
}

const char *FM25V02A::getErrorString(FM25V02A_Error error)
{
    /* Convert negative error codes to index */
    const int8_t index = (error <= 0) ? static_cast<int8_t>(-error) : 0;

    /* NASA Rule 5: Bounds check assertion */
    FM25V02A_ASSERT(index < ERROR_STRING_COUNT);

    if (index >= ERROR_STRING_COUNT) {
        return "Unknown error";
    }

    return ERROR_STRINGS[index];
}

FM25V02A_Error FM25V02A::writeEnable(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(!m_state.asleep);

    /* Send WREN command */
    beginTransaction();
    m_spi->transfer(OPCODE_WREN);
    endTransaction();

    /* Verify WEL bit is set */
    uint8_t status = 0U;
    FM25V02A_Error err = readStatus(&status);

    if (err != FM25V02A_OK) {
        return err;
    }

    if ((status & FM25V02A_STATUS_WEL) == 0U) {
        reportError(FM25V02A_ERR_WRITE_ENABLE, 0U);
        return FM25V02A_ERR_WRITE_ENABLE;
    }

    return FM25V02A_OK;
}

FM25V02A_Error FM25V02A::writeDisable(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(!m_state.asleep);

    beginTransaction();
    m_spi->transfer(OPCODE_WRDI);
    endTransaction();

    return FM25V02A_OK;
}

void FM25V02A::beginTransaction(void)
{
    m_spi->beginTransaction(m_spiSettings);
    digitalWrite(m_csPin, LOW);
}

void FM25V02A::endTransaction(void)
{
    digitalWrite(m_csPin, HIGH);
    m_spi->endTransaction();
}

void FM25V02A::sendOpcode(uint8_t opcode)
{
    beginTransaction();
    m_spi->transfer(opcode);
    endTransaction();
}

FM25V02A_Error FM25V02A::validateAddressAndSize(uint16_t address, uint16_t size) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(size <= FM25V02A_MAX_TRANSFER_SIZE);
    FM25V02A_ASSERT(address <= FM25V02A_MAX_ADDRESS);

    if (size == 0U) {
        return FM25V02A_ERR_INVALID_SIZE;
    }

    if (size > FM25V02A_MAX_TRANSFER_SIZE) {
        return FM25V02A_ERR_INVALID_SIZE;
    }

    if (address > FM25V02A_MAX_ADDRESS) {
        return FM25V02A_ERR_INVALID_ADDRESS;
    }

    /* Check for overflow using 32-bit arithmetic */
    const uint32_t endAddress = static_cast<uint32_t>(address) + size;
    if (endAddress > FM25V02A_MEMORY_SIZE) {
        return FM25V02A_ERR_ADDRESS_OVERFLOW;
    }

    return FM25V02A_OK;
}

bool FM25V02A::isWriteProtected(uint16_t address, uint16_t size) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(size > 0U);
    FM25V02A_ASSERT(address <= FM25V02A_MAX_ADDRESS);

    /* Get protection level from cached status */
    const uint8_t bpBits = m_state.status & 0x0CU;
    const uint32_t endAddress = static_cast<uint32_t>(address) + size - 1U;

    /* Check protection based on BP bits */
    switch (bpBits) {
        case FM25V02A_PROTECT_NONE:
            return false;

        case FM25V02A_PROTECT_UPPER_QUARTER:
            /* Protected: 0x6000 - 0x7FFF */
            return (endAddress >= 0x6000U);

        case FM25V02A_PROTECT_UPPER_HALF:
            /* Protected: 0x4000 - 0x7FFF */
            return (endAddress >= 0x4000U);

        case FM25V02A_PROTECT_ALL:
            return true;

        default:
            /* Unknown protection state - assume protected */
            return true;
    }
}

void FM25V02A::reportError(FM25V02A_Error error, uint16_t address)
{
    if (m_errorCallback != nullptr) {
        m_errorCallback(error, address, m_errorContext);
    }
}

FM25V02A_Error FM25V02A::refreshProtectionStatus(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_ASSERT(m_spi != nullptr);
    FM25V02A_ASSERT(m_state.initialized);

    if (!m_state.initialized) {
        return FM25V02A_ERR_NOT_INITIALIZED;
    }

    if (m_state.asleep) {
        return FM25V02A_ERR_ASLEEP;
    }

    /* Read fresh status from hardware */
    uint8_t status = 0U;
    beginTransaction();
    m_spi->transfer(OPCODE_RDSR);
    status = m_spi->transfer(0x00U);
    endTransaction();

    m_state.status = status;
    return FM25V02A_OK;
}
