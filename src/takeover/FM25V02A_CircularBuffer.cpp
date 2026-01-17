/**
 * @file FM25V02A_CircularBuffer.cpp
 * @brief Circular Buffer implementation using FM25V02A FRAM
 *
 * Implementation follows NASA's 10 Rules of Safe Code.
 *
 * Power-fail protection is implemented using double-buffered headers:
 * - Two copies of the header are maintained (slot A and slot B)
 * - Each write increments the sequence number and alternates slots
 * - On recovery, the header with the higher valid sequence is used
 * - This ensures atomicity even if power fails during header write
 */

#include "FM25V02A_CircularBuffer.h"

/**
 * @brief Assertion macro - halts on failure (NASA Rule 5)
 */
#if FM25V02A_DEBUG
#define FM25V02A_CB_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            Serial.printf("CB ASSERT FAILED: %s:%d - %s\n", __FILE__, __LINE__, #cond); \
            Serial.flush(); \
            while (true) { \
                /* Halt execution */ \
            } \
        } \
    } while (false)
#else
#define FM25V02A_CB_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            while (true) { \
                /* Halt execution */ \
            } \
        } \
    } while (false)
#endif

/**
 * @brief Static error strings
 */
static const char *const CB_ERROR_STRINGS[] = {
    "OK",                  /* FM25V02A_CB_OK */
    "Null pointer",        /* FM25V02A_CB_ERR_NULL_POINTER */
    "Invalid parameter",   /* FM25V02A_CB_ERR_INVALID_PARAM */
    "Not initialized",     /* FM25V02A_CB_ERR_NOT_INIT */
    "Buffer full",         /* FM25V02A_CB_ERR_FULL */
    "Buffer empty",        /* FM25V02A_CB_ERR_EMPTY */
    "FRAM error",          /* FM25V02A_CB_ERR_FRAM */
    "Buffer corrupted",    /* FM25V02A_CB_ERR_CORRUPTED */
    "Size mismatch"        /* FM25V02A_CB_ERR_SIZE_MISMATCH */
};

static const uint8_t CB_ERROR_STRING_COUNT = 9U;

FM25V02A_CircularBuffer::FM25V02A_CircularBuffer(FM25V02A *fram, uint16_t baseAddress,
                                                   uint16_t entrySize, uint16_t maxEntries,
                                                   bool overwriteOnFull)
    : m_fram(fram)
    , m_baseAddress(baseAddress)
    , m_entrySize(entrySize)
    , m_maxEntries(maxEntries)
    , m_overwriteOnFull(overwriteOnFull)
    , m_initialized(false)
    , m_activeSlot(FM25V02A_CB_HEADER_SLOT_A)
    , m_header{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(fram != nullptr);
    FM25V02A_CB_ASSERT(entrySize > 0U);
    FM25V02A_CB_ASSERT(entrySize <= FM25V02A_CB_MAX_ENTRY_SIZE);
    FM25V02A_CB_ASSERT(maxEntries > 0U);

    /* Validate total size fits in FRAM */
    const uint32_t totalSize = FM25V02A_CB_HEADER_SIZE +
                               (static_cast<uint32_t>(entrySize) * maxEntries);
    FM25V02A_CB_ASSERT((baseAddress + totalSize) <= FM25V02A_MEMORY_SIZE);
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::init(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_fram != nullptr);
    FM25V02A_CB_ASSERT(m_fram->isInitialized());

    if (m_fram == nullptr) {
        return FM25V02A_CB_ERR_NULL_POINTER;
    }

    if (!m_fram->isInitialized()) {
        return FM25V02A_CB_ERR_FRAM;
    }

    /* Try to load existing header with power-fail recovery */
    FM25V02A_CB_Error err = loadHeader();

    if (err == FM25V02A_CB_OK) {
        /* Valid header found - verify it matches our configuration */
        if ((m_header.entrySize != m_entrySize) ||
            (m_header.maxEntries != m_maxEntries)) {
            /* Configuration mismatch - reformat */
            err = format();
        } else {
            m_initialized = true;
        }
    } else {
        /* No valid header - format new buffer */
        err = format();
    }

    return err;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::format(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_fram != nullptr);
    FM25V02A_CB_ASSERT(m_entrySize > 0U);

    /* Initialize header */
    m_header.magic = FM25V02A_CB_MAGIC;
    m_header.entrySize = m_entrySize;
    m_header.maxEntries = m_maxEntries;
    m_header.head = 0U;
    m_header.tail = 0U;
    m_header.count = 0U;
    m_header.sequence = 0U;
    m_activeSlot = FM25V02A_CB_HEADER_SLOT_A;

    /* Write header to both slots for fresh format */
    m_header.headerCrc = calculateHeaderCrc(&m_header);
    FM25V02A_CB_Error err = saveHeaderToSlot(FM25V02A_CB_HEADER_SLOT_A);
    if (err != FM25V02A_CB_OK) {
        return err;
    }

    err = saveHeaderToSlot(FM25V02A_CB_HEADER_SLOT_B);
    if (err == FM25V02A_CB_OK) {
        m_initialized = true;
    }

    return err;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::write(const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(data != nullptr);
    FM25V02A_CB_ASSERT(size == m_entrySize);

    /* NASA Rule 7: Parameter validation */
    if (data == nullptr) {
        return FM25V02A_CB_ERR_NULL_POINTER;
    }

    if (!m_initialized) {
        return FM25V02A_CB_ERR_NOT_INIT;
    }

    if (size != m_entrySize) {
        return FM25V02A_CB_ERR_SIZE_MISMATCH;
    }

    /* Check if full and handle accordingly */
    if (m_header.count >= m_maxEntries) {
        if (!m_overwriteOnFull) {
            return FM25V02A_CB_ERR_FULL;
        }

        /* Overwrite mode: advance tail to discard oldest */
        m_header.tail = (m_header.tail + 1U) % m_maxEntries;
        m_header.count--; /* Will be incremented below */
    }

    /* Calculate entry address */
    const uint16_t entryAddr = getEntryAddress(m_header.head);

    /* Write entry data first (before header update for crash safety) */
    FM25V02A_Error framErr = m_fram->write(entryAddr, data, size);
    if (framErr != FM25V02A_OK) {
        return FM25V02A_CB_ERR_FRAM;
    }

    /* Update header */
    m_header.head = (m_header.head + 1U) % m_maxEntries;
    m_header.count++;

    /* Save updated header with power-fail protection */
    FM25V02A_CB_Error err = saveHeader();

    return err;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::read(uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(data != nullptr);
    FM25V02A_CB_ASSERT(size >= m_entrySize);

    FM25V02A_CB_Error err = peek(data, size);
    if (err != FM25V02A_CB_OK) {
        return err;
    }

    /* Remove the entry */
    return pop();
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::peek(uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(data != nullptr);
    FM25V02A_CB_ASSERT(size >= m_entrySize);

    /* NASA Rule 7: Parameter validation */
    if (data == nullptr) {
        return FM25V02A_CB_ERR_NULL_POINTER;
    }

    if (!m_initialized) {
        return FM25V02A_CB_ERR_NOT_INIT;
    }

    if (size < m_entrySize) {
        return FM25V02A_CB_ERR_SIZE_MISMATCH;
    }

    if (m_header.count == 0U) {
        return FM25V02A_CB_ERR_EMPTY;
    }

    /* Calculate entry address */
    const uint16_t entryAddr = getEntryAddress(m_header.tail);

    /* Read entry data */
    FM25V02A_Error framErr = m_fram->read(entryAddr, data, m_entrySize);
    if (framErr != FM25V02A_OK) {
        return FM25V02A_CB_ERR_FRAM;
    }

    return FM25V02A_CB_OK;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::readAt(uint16_t index, uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(data != nullptr);
    FM25V02A_CB_ASSERT(index < m_header.count);

    /* NASA Rule 7: Parameter validation */
    if (data == nullptr) {
        return FM25V02A_CB_ERR_NULL_POINTER;
    }

    if (!m_initialized) {
        return FM25V02A_CB_ERR_NOT_INIT;
    }

    if (size < m_entrySize) {
        return FM25V02A_CB_ERR_SIZE_MISMATCH;
    }

    if (index >= m_header.count) {
        return FM25V02A_CB_ERR_INVALID_PARAM;
    }

    /* Calculate actual position in circular buffer */
    const uint16_t actualIndex = (m_header.tail + index) % m_maxEntries;
    const uint16_t entryAddr = getEntryAddress(actualIndex);

    /* Read entry data */
    FM25V02A_Error framErr = m_fram->read(entryAddr, data, m_entrySize);
    if (framErr != FM25V02A_OK) {
        return FM25V02A_CB_ERR_FRAM;
    }

    return FM25V02A_CB_OK;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::pop(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_initialized);
    FM25V02A_CB_ASSERT(m_header.count > 0U);

    if (!m_initialized) {
        return FM25V02A_CB_ERR_NOT_INIT;
    }

    if (m_header.count == 0U) {
        return FM25V02A_CB_ERR_EMPTY;
    }

    /* Advance tail */
    m_header.tail = (m_header.tail + 1U) % m_maxEntries;
    m_header.count--;

    /* Save updated header with power-fail protection */
    return saveHeader();
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::clear(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_initialized);
    FM25V02A_CB_ASSERT(m_fram != nullptr);

    if (!m_initialized) {
        return FM25V02A_CB_ERR_NOT_INIT;
    }

    /* Reset indices */
    m_header.head = 0U;
    m_header.tail = 0U;
    m_header.count = 0U;

    /* Save updated header with power-fail protection */
    return saveHeader();
}

uint16_t FM25V02A_CircularBuffer::getCount(void) const
{
    return m_initialized ? m_header.count : 0U;
}

uint16_t FM25V02A_CircularBuffer::getCapacity(void) const
{
    return m_maxEntries;
}

bool FM25V02A_CircularBuffer::isEmpty(void) const
{
    return (m_header.count == 0U);
}

bool FM25V02A_CircularBuffer::isFull(void) const
{
    return (m_header.count >= m_maxEntries);
}

uint16_t FM25V02A_CircularBuffer::getAvailable(void) const
{
    if (!m_initialized) {
        return 0U;
    }
    return m_maxEntries - m_header.count;
}

uint16_t FM25V02A_CircularBuffer::getEntrySize(void) const
{
    return m_entrySize;
}

bool FM25V02A_CircularBuffer::isInitialized(void) const
{
    return m_initialized;
}

const char *FM25V02A_CircularBuffer::getErrorString(FM25V02A_CB_Error error)
{
    const int8_t index = (error <= 0) ? static_cast<int8_t>(-error) : 0;

    /* NASA Rule 5: Bounds check */
    FM25V02A_CB_ASSERT(index < CB_ERROR_STRING_COUNT);

    if (index >= CB_ERROR_STRING_COUNT) {
        return "Unknown error";
    }

    return CB_ERROR_STRINGS[index];
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::loadHeader(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_fram != nullptr);
    FM25V02A_CB_ASSERT(m_fram->isInitialized());

    FM25V02A_CB_Header headerA;
    FM25V02A_CB_Header headerB;

    /* Load both header slots */
    FM25V02A_CB_Error errA = loadHeaderFromSlot(FM25V02A_CB_HEADER_SLOT_A, &headerA);
    FM25V02A_CB_Error errB = loadHeaderFromSlot(FM25V02A_CB_HEADER_SLOT_B, &headerB);

    bool validA = (errA == FM25V02A_CB_OK) && validateHeader(&headerA);
    bool validB = (errB == FM25V02A_CB_OK) && validateHeader(&headerB);

    /* Power-fail recovery: select header with higher sequence number */
    if (validA && validB) {
        /*
         * Both valid - use the one with higher sequence number.
         * Handle sequence wrap-around: if difference > 32768, the smaller
         * number has wrapped and is actually newer.
         */
        uint16_t diff = headerA.sequence - headerB.sequence;
        if (diff == 0U) {
            /* Same sequence - use slot A */
            m_header = headerA;
            m_activeSlot = FM25V02A_CB_HEADER_SLOT_A;
        } else if (diff < 32768U) {
            /* A is newer */
            m_header = headerA;
            m_activeSlot = FM25V02A_CB_HEADER_SLOT_A;
        } else {
            /* B is newer (A wrapped) */
            m_header = headerB;
            m_activeSlot = FM25V02A_CB_HEADER_SLOT_B;
        }
        return FM25V02A_CB_OK;
    } else if (validA) {
        m_header = headerA;
        m_activeSlot = FM25V02A_CB_HEADER_SLOT_A;
        return FM25V02A_CB_OK;
    } else if (validB) {
        m_header = headerB;
        m_activeSlot = FM25V02A_CB_HEADER_SLOT_B;
        return FM25V02A_CB_OK;
    }

    /* Neither header valid - buffer is corrupted or uninitialized */
    return FM25V02A_CB_ERR_CORRUPTED;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::saveHeader(void)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(m_fram != nullptr);
    FM25V02A_CB_ASSERT(m_initialized);

    /*
     * Power-fail safe write sequence:
     * 1. Increment sequence number
     * 2. Calculate new CRC
     * 3. Write to alternate slot (not the current active slot)
     * 4. Update active slot pointer
     *
     * This ensures that if power fails during the write, the old
     * header in the other slot remains valid and will be recovered.
     */

    /* Increment sequence number */
    m_header.sequence++;

    /* Calculate new CRC */
    m_header.headerCrc = calculateHeaderCrc(&m_header);

    /* Write to the alternate slot */
    uint8_t newSlot = (m_activeSlot == FM25V02A_CB_HEADER_SLOT_A) ?
                       FM25V02A_CB_HEADER_SLOT_B : FM25V02A_CB_HEADER_SLOT_A;

    FM25V02A_CB_Error err = saveHeaderToSlot(newSlot);
    if (err == FM25V02A_CB_OK) {
        /* Update active slot only on successful write */
        m_activeSlot = newSlot;
    }

    return err;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::loadHeaderFromSlot(uint8_t slot, FM25V02A_CB_Header *header)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(slot <= FM25V02A_CB_HEADER_SLOT_B);
    FM25V02A_CB_ASSERT(header != nullptr);

    if (header == nullptr) {
        return FM25V02A_CB_ERR_NULL_POINTER;
    }

    uint8_t headerBytes[FM25V02A_CB_SINGLE_HEADER_SIZE];
    const uint16_t slotAddr = getHeaderSlotAddress(slot);

    /* Read header from FRAM */
    FM25V02A_Error framErr = m_fram->read(slotAddr, headerBytes, FM25V02A_CB_SINGLE_HEADER_SIZE);
    if (framErr != FM25V02A_OK) {
        return FM25V02A_CB_ERR_FRAM;
    }

    /* Parse header - big-endian format */
    header->magic = (static_cast<uint32_t>(headerBytes[0U]) << 24U) |
                    (static_cast<uint32_t>(headerBytes[1U]) << 16U) |
                    (static_cast<uint32_t>(headerBytes[2U]) << 8U) |
                     static_cast<uint32_t>(headerBytes[3U]);

    header->entrySize = (static_cast<uint16_t>(headerBytes[4U]) << 8U) |
                         static_cast<uint16_t>(headerBytes[5U]);

    header->maxEntries = (static_cast<uint16_t>(headerBytes[6U]) << 8U) |
                          static_cast<uint16_t>(headerBytes[7U]);

    header->head = (static_cast<uint16_t>(headerBytes[8U]) << 8U) |
                    static_cast<uint16_t>(headerBytes[9U]);

    header->tail = (static_cast<uint16_t>(headerBytes[10U]) << 8U) |
                    static_cast<uint16_t>(headerBytes[11U]);

    header->count = (static_cast<uint16_t>(headerBytes[12U]) << 8U) |
                     static_cast<uint16_t>(headerBytes[13U]);

    header->sequence = (static_cast<uint16_t>(headerBytes[14U]) << 8U) |
                        static_cast<uint16_t>(headerBytes[15U]);

    header->headerCrc = (static_cast<uint16_t>(headerBytes[16U]) << 8U) |
                         static_cast<uint16_t>(headerBytes[17U]);

    return FM25V02A_CB_OK;
}

FM25V02A_CB_Error FM25V02A_CircularBuffer::saveHeaderToSlot(uint8_t slot)
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(slot <= FM25V02A_CB_HEADER_SLOT_B);
    FM25V02A_CB_ASSERT(m_fram != nullptr);

    uint8_t headerBytes[FM25V02A_CB_SINGLE_HEADER_SIZE];
    const uint16_t slotAddr = getHeaderSlotAddress(slot);

    /* Serialize header - big-endian format */
    headerBytes[0U] = static_cast<uint8_t>((m_header.magic >> 24U) & 0xFFU);
    headerBytes[1U] = static_cast<uint8_t>((m_header.magic >> 16U) & 0xFFU);
    headerBytes[2U] = static_cast<uint8_t>((m_header.magic >> 8U) & 0xFFU);
    headerBytes[3U] = static_cast<uint8_t>(m_header.magic & 0xFFU);

    headerBytes[4U] = static_cast<uint8_t>((m_header.entrySize >> 8U) & 0xFFU);
    headerBytes[5U] = static_cast<uint8_t>(m_header.entrySize & 0xFFU);

    headerBytes[6U] = static_cast<uint8_t>((m_header.maxEntries >> 8U) & 0xFFU);
    headerBytes[7U] = static_cast<uint8_t>(m_header.maxEntries & 0xFFU);

    headerBytes[8U] = static_cast<uint8_t>((m_header.head >> 8U) & 0xFFU);
    headerBytes[9U] = static_cast<uint8_t>(m_header.head & 0xFFU);

    headerBytes[10U] = static_cast<uint8_t>((m_header.tail >> 8U) & 0xFFU);
    headerBytes[11U] = static_cast<uint8_t>(m_header.tail & 0xFFU);

    headerBytes[12U] = static_cast<uint8_t>((m_header.count >> 8U) & 0xFFU);
    headerBytes[13U] = static_cast<uint8_t>(m_header.count & 0xFFU);

    headerBytes[14U] = static_cast<uint8_t>((m_header.sequence >> 8U) & 0xFFU);
    headerBytes[15U] = static_cast<uint8_t>(m_header.sequence & 0xFFU);

    headerBytes[16U] = static_cast<uint8_t>((m_header.headerCrc >> 8U) & 0xFFU);
    headerBytes[17U] = static_cast<uint8_t>(m_header.headerCrc & 0xFFU);

    /* Write header to FRAM */
    FM25V02A_Error framErr = m_fram->write(slotAddr, headerBytes, FM25V02A_CB_SINGLE_HEADER_SIZE);
    if (framErr != FM25V02A_OK) {
        return FM25V02A_CB_ERR_FRAM;
    }

    return FM25V02A_CB_OK;
}

bool FM25V02A_CircularBuffer::validateHeader(const FM25V02A_CB_Header *header) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(header != nullptr);

    if (header == nullptr) {
        return false;
    }

    /* Check magic number */
    if (header->magic != FM25V02A_CB_MAGIC) {
        return false;
    }

    /* Validate CRC */
    const uint16_t calculatedCrc = calculateHeaderCrc(header);
    if (calculatedCrc != header->headerCrc) {
        return false;
    }

    /* Validate indices are in range */
    if (header->head >= header->maxEntries) {
        return false;
    }

    if (header->tail >= header->maxEntries) {
        return false;
    }

    if (header->count > header->maxEntries) {
        return false;
    }

    return true;
}

uint16_t FM25V02A_CircularBuffer::getEntryAddress(uint16_t index) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(index < m_maxEntries);
    FM25V02A_CB_ASSERT(m_entrySize > 0U);

    return m_baseAddress + FM25V02A_CB_HEADER_SIZE +
           (index * m_entrySize);
}

uint16_t FM25V02A_CircularBuffer::calculateHeaderCrc(const FM25V02A_CB_Header *header) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(header != nullptr);

    /* Calculate CRC over bytes 0-15 (excluding CRC field at 16-17) */
    uint8_t headerBytes[16U];

    headerBytes[0U] = static_cast<uint8_t>((header->magic >> 24U) & 0xFFU);
    headerBytes[1U] = static_cast<uint8_t>((header->magic >> 16U) & 0xFFU);
    headerBytes[2U] = static_cast<uint8_t>((header->magic >> 8U) & 0xFFU);
    headerBytes[3U] = static_cast<uint8_t>(header->magic & 0xFFU);

    headerBytes[4U] = static_cast<uint8_t>((header->entrySize >> 8U) & 0xFFU);
    headerBytes[5U] = static_cast<uint8_t>(header->entrySize & 0xFFU);

    headerBytes[6U] = static_cast<uint8_t>((header->maxEntries >> 8U) & 0xFFU);
    headerBytes[7U] = static_cast<uint8_t>(header->maxEntries & 0xFFU);

    headerBytes[8U] = static_cast<uint8_t>((header->head >> 8U) & 0xFFU);
    headerBytes[9U] = static_cast<uint8_t>(header->head & 0xFFU);

    headerBytes[10U] = static_cast<uint8_t>((header->tail >> 8U) & 0xFFU);
    headerBytes[11U] = static_cast<uint8_t>(header->tail & 0xFFU);

    headerBytes[12U] = static_cast<uint8_t>((header->count >> 8U) & 0xFFU);
    headerBytes[13U] = static_cast<uint8_t>(header->count & 0xFFU);

    headerBytes[14U] = static_cast<uint8_t>((header->sequence >> 8U) & 0xFFU);
    headerBytes[15U] = static_cast<uint8_t>(header->sequence & 0xFFU);

    return FM25V02A::calculateCRC16(headerBytes, 16U);
}

uint16_t FM25V02A_CircularBuffer::getHeaderSlotAddress(uint8_t slot) const
{
    /* NASA Rule 5: Assertions */
    FM25V02A_CB_ASSERT(slot <= FM25V02A_CB_HEADER_SLOT_B);

    return m_baseAddress + (slot * FM25V02A_CB_SINGLE_HEADER_SIZE);
}
