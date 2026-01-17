/**
 * @file FM25V02A_CircularBuffer.h
 * @brief Circular Buffer implementation using FM25V02A FRAM
 *
 * Provides a persistent circular buffer for data logging stored in FRAM.
 * Follows NASA's 10 Rules of Safe Code.
 *
 * Power-fail protection is implemented using double-buffered headers.
 * Two copies of the header are maintained, and on recovery the one with
 * the higher valid sequence number is used.
 *
 * Memory Layout (36 bytes header + entries):
 *   [Header A: 18 bytes][Header B: 18 bytes][Entry 0][Entry 1]...[Entry N-1]
 *
 * Each Header Structure (18 bytes):
 *   Bytes 0-3:   Magic number (0x46524D42 = "FRMB")
 *   Bytes 4-5:   Entry size
 *   Bytes 6-7:   Max entries (capacity)
 *   Bytes 8-9:   Head index (next write position)
 *   Bytes 10-11: Tail index (oldest entry position)
 *   Bytes 12-13: Entry count
 *   Bytes 14-15: Sequence number (for power-fail recovery)
 *   Bytes 16-17: Header CRC16 (over bytes 0-15)
 */

#ifndef FM25V02A_CIRCULAR_BUFFER_H
#define FM25V02A_CIRCULAR_BUFFER_H

#include "FM25V02A.h"

/**
 * @brief Single header size in bytes (18 bytes with sequence number)
 */
#define FM25V02A_CB_SINGLE_HEADER_SIZE 18U

/**
 * @brief Total header size in bytes (2 headers for double-buffering)
 * Double-buffered headers provide power-fail protection.
 */
#define FM25V02A_CB_HEADER_SIZE (FM25V02A_CB_SINGLE_HEADER_SIZE * 2U)

/**
 * @brief Magic number for buffer validation ("FRMB")
 */
#define FM25V02A_CB_MAGIC 0x46524D42U

/**
 * @brief Maximum entry size (including CRC)
 */
#define FM25V02A_CB_MAX_ENTRY_SIZE 254U

/**
 * @brief Header slot indices for double-buffering
 */
#define FM25V02A_CB_HEADER_SLOT_A 0U
#define FM25V02A_CB_HEADER_SLOT_B 1U

/**
 * @brief Circular buffer error codes
 */
typedef enum {
    FM25V02A_CB_OK = 0,                 /**< Operation successful */
    FM25V02A_CB_ERR_NULL_POINTER = -1,  /**< Null pointer passed */
    FM25V02A_CB_ERR_INVALID_PARAM = -2, /**< Invalid parameter */
    FM25V02A_CB_ERR_NOT_INIT = -3,      /**< Buffer not initialized */
    FM25V02A_CB_ERR_FULL = -4,          /**< Buffer is full (non-overwrite mode) */
    FM25V02A_CB_ERR_EMPTY = -5,         /**< Buffer is empty */
    FM25V02A_CB_ERR_FRAM = -6,          /**< FRAM operation failed */
    FM25V02A_CB_ERR_CORRUPTED = -7,     /**< Buffer header corrupted */
    FM25V02A_CB_ERR_SIZE_MISMATCH = -8  /**< Entry size doesn't match */
} FM25V02A_CB_Error;

/**
 * @brief Circular buffer header structure (stored in FRAM)
 *
 * Power-fail protection using double-buffered headers:
 * - Two copies of the header are maintained (slot A and slot B)
 * - Each write increments the sequence number
 * - On recovery, the header with the higher valid sequence is used
 * - This ensures atomicity even if power fails during header write
 *
 * Memory layout (18 bytes per header, 36 bytes total):
 *   Bytes 0-3:   Magic number (0x46524D42 = "FRMB")
 *   Bytes 4-5:   Entry size
 *   Bytes 6-7:   Max entries (capacity)
 *   Bytes 8-9:   Head index (next write position)
 *   Bytes 10-11: Tail index (oldest entry position)
 *   Bytes 12-13: Entry count
 *   Bytes 14-15: Sequence number (for power-fail recovery)
 *   Bytes 16-17: Header CRC16 (over bytes 0-15)
 */
typedef struct {
    uint32_t magic;       /**< Magic number for validation */
    uint16_t entrySize;   /**< Size of each entry in bytes */
    uint16_t maxEntries;  /**< Maximum number of entries */
    uint16_t head;        /**< Next write position */
    uint16_t tail;        /**< Oldest entry position */
    uint16_t count;       /**< Current number of entries */
    uint16_t sequence;    /**< Sequence number for power-fail recovery */
    uint16_t headerCrc;   /**< CRC16 of header (bytes 0-15) */
} FM25V02A_CB_Header;

/**
 * @brief Circular Buffer Class for FM25V02A FRAM
 *
 * Provides persistent circular buffer functionality with optional CRC
 * verification on entries. Supports both overwrite and non-overwrite modes.
 */
class FM25V02A_CircularBuffer {
public:
    /**
     * @brief Construct circular buffer instance
     *
     * @param fram Pointer to initialized FM25V02A instance
     * @param baseAddress Starting address in FRAM for buffer
     * @param entrySize Size of each entry in bytes (1-254)
     * @param maxEntries Maximum number of entries to store
     * @param overwriteOnFull True to overwrite oldest entries when full
     *
     * @note NASA Rule 3: Uses provided FRAM instance, no dynamic allocation
     */
    FM25V02A_CircularBuffer(FM25V02A *fram, uint16_t baseAddress,
                            uint16_t entrySize, uint16_t maxEntries,
                            bool overwriteOnFull = true);

    /**
     * @brief Initialize or recover the circular buffer
     *
     * If valid header found, recovers existing buffer state.
     * If no valid header, formats new buffer.
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error init(void);

    /**
     * @brief Format buffer, erasing all entries
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error format(void);

    /**
     * @brief Write an entry to the buffer
     *
     * @param data Entry data to write
     * @param size Size of data (must match entrySize)
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error write(const uint8_t *data, uint16_t size);

    /**
     * @brief Read the oldest entry from the buffer
     *
     * @param data Buffer to store entry data
     * @param size Size of buffer (must be >= entrySize)
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error read(uint8_t *data, uint16_t size);

    /**
     * @brief Peek at the oldest entry without removing it
     *
     * @param data Buffer to store entry data
     * @param size Size of buffer (must be >= entrySize)
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error peek(uint8_t *data, uint16_t size);

    /**
     * @brief Read entry at specific index (0 = oldest)
     *
     * @param index Entry index (0 to count-1)
     * @param data Buffer to store entry data
     * @param size Size of buffer (must be >= entrySize)
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error readAt(uint16_t index, uint8_t *data, uint16_t size);

    /**
     * @brief Remove the oldest entry
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error pop(void);

    /**
     * @brief Clear all entries (reset head/tail)
     *
     * @return FM25V02A_CB_OK on success, error code otherwise
     */
    FM25V02A_CB_Error clear(void);

    /**
     * @brief Get number of entries in buffer
     *
     * @return Entry count
     */
    uint16_t getCount(void) const;

    /**
     * @brief Get maximum capacity
     *
     * @return Maximum number of entries
     */
    uint16_t getCapacity(void) const;

    /**
     * @brief Check if buffer is empty
     *
     * @return true if empty
     */
    bool isEmpty(void) const;

    /**
     * @brief Check if buffer is full
     *
     * @return true if full
     */
    bool isFull(void) const;

    /**
     * @brief Get available space
     *
     * @return Number of entries that can be added
     */
    uint16_t getAvailable(void) const;

    /**
     * @brief Get entry size
     *
     * @return Size of each entry in bytes
     */
    uint16_t getEntrySize(void) const;

    /**
     * @brief Check if buffer is initialized
     *
     * @return true if initialized successfully
     */
    bool isInitialized(void) const;

    /**
     * @brief Get error string description
     *
     * @param error Error code
     *
     * @return Static string describing error
     */
    static const char *getErrorString(FM25V02A_CB_Error error);

private:
    /**
     * @brief Load header from FRAM with power-fail recovery
     *
     * Loads both header slots and selects the one with the higher
     * valid sequence number for power-fail recovery.
     *
     * @return FM25V02A_CB_OK if valid header loaded
     */
    FM25V02A_CB_Error loadHeader(void);

    /**
     * @brief Save header to FRAM with power-fail protection
     *
     * Uses double-buffered writes: increments sequence number and
     * alternates between header slots A and B. This ensures that
     * at least one valid header always exists even if power fails
     * during a write.
     *
     * @return FM25V02A_CB_OK on success
     */
    FM25V02A_CB_Error saveHeader(void);

    /**
     * @brief Load header from specific slot
     *
     * @param slot Header slot (FM25V02A_CB_HEADER_SLOT_A or _B)
     * @param header Output header structure
     *
     * @return FM25V02A_CB_OK if valid header loaded
     */
    FM25V02A_CB_Error loadHeaderFromSlot(uint8_t slot, FM25V02A_CB_Header *header);

    /**
     * @brief Save header to specific slot
     *
     * @param slot Header slot (FM25V02A_CB_HEADER_SLOT_A or _B)
     *
     * @return FM25V02A_CB_OK on success
     */
    FM25V02A_CB_Error saveHeaderToSlot(uint8_t slot);

    /**
     * @brief Validate header CRC and magic number
     *
     * @param header Header to validate
     *
     * @return true if header is valid
     */
    bool validateHeader(const FM25V02A_CB_Header *header) const;

    /**
     * @brief Calculate address for entry at given index
     *
     * @param index Entry index in buffer
     *
     * @return FRAM address of entry
     */
    uint16_t getEntryAddress(uint16_t index) const;

    /**
     * @brief Calculate CRC for header (excluding headerCrc field)
     *
     * @param header Header to calculate CRC for
     *
     * @return Calculated CRC16
     */
    uint16_t calculateHeaderCrc(const FM25V02A_CB_Header *header) const;

    /**
     * @brief Get address of header slot
     *
     * @param slot Header slot (0 or 1)
     *
     * @return FRAM address of header slot
     */
    uint16_t getHeaderSlotAddress(uint8_t slot) const;

    FM25V02A *m_fram;           /**< FRAM driver instance */
    uint16_t m_baseAddress;     /**< Base address in FRAM */
    uint16_t m_entrySize;       /**< Size of each entry */
    uint16_t m_maxEntries;      /**< Maximum entries */
    bool m_overwriteOnFull;     /**< Overwrite oldest when full */
    bool m_initialized;         /**< Initialization state */
    uint8_t m_activeSlot;       /**< Currently active header slot */
    FM25V02A_CB_Header m_header; /**< Cached header */
};

#endif /* FM25V02A_CIRCULAR_BUFFER_H */
