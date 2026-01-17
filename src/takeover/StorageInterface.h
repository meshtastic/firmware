/**
 * @file StorageInterface.h
 * @brief Abstract storage interface for FRAM/Flash graceful degradation
 *
 * Provides a common interface for non-volatile storage backends,
 * enabling graceful fallback from FRAM to Flash memory when failures occur.
 *
 * Follows NASA's 10 Rules of Safe Code.
 */

#ifndef STORAGE_INTERFACE_H
#define STORAGE_INTERFACE_H

#include <stdint.h>

/**
 * @brief Storage type identifiers
 */
typedef enum {
    STORAGE_TYPE_UNKNOWN = 0,
    STORAGE_TYPE_FRAM = 1,      /**< FRAM - fast, high endurance */
    STORAGE_TYPE_FLASH = 2,     /**< Flash - slower, limited endurance */
    STORAGE_TYPE_EEPROM = 3     /**< EEPROM - slow, moderate endurance */
} StorageType;

/**
 * @brief Storage error codes
 */
typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_NULL_POINTER = -1,
    STORAGE_ERR_INVALID_ADDRESS = -2,
    STORAGE_ERR_INVALID_SIZE = -3,
    STORAGE_ERR_NOT_INITIALIZED = -4,
    STORAGE_ERR_WRITE_FAILED = -5,
    STORAGE_ERR_READ_FAILED = -6,
    STORAGE_ERR_WRITE_PROTECTED = -7,
    STORAGE_ERR_DEVICE_BUSY = -8,
    STORAGE_ERR_WEAR_LIMIT = -9,        /**< Flash wear limit reached */
    STORAGE_ERR_ERASE_FAILED = -10,
    STORAGE_ERR_VERIFY_FAILED = -11
} StorageError;

/**
 * @brief Storage health status
 */
typedef struct {
    bool initialized;           /**< Storage is initialized */
    bool healthy;               /**< Storage is functioning normally */
    bool degraded;              /**< Storage has partial failures */
    uint32_t errorCount;        /**< Cumulative error count */
    uint32_t writeCount;        /**< Total write operations */
    uint8_t healthPercent;      /**< Estimated health 0-100% */
} StorageHealth;

/**
 * @brief Abstract Storage Interface
 *
 * Base class for all storage backends. Enables graceful degradation
 * by allowing seamless switching between storage types.
 */
class IStorage {
public:
    virtual ~IStorage() = default;

    /**
     * @brief Initialize the storage device
     * @return STORAGE_OK on success
     */
    virtual StorageError init(void) = 0;

    /**
     * @brief Read data from storage
     * @param address Starting address
     * @param buffer Buffer to store read data
     * @param size Number of bytes to read
     * @return STORAGE_OK on success
     */
    virtual StorageError read(uint32_t address, uint8_t *buffer, uint16_t size) = 0;

    /**
     * @brief Write data to storage
     * @param address Starting address
     * @param data Data to write
     * @param size Number of bytes to write
     * @return STORAGE_OK on success
     */
    virtual StorageError write(uint32_t address, const uint8_t *data, uint16_t size) = 0;

    /**
     * @brief Erase a region of storage (required for Flash)
     * @param address Starting address
     * @param size Number of bytes to erase
     * @return STORAGE_OK on success (no-op for FRAM)
     */
    virtual StorageError erase(uint32_t address, uint16_t size) = 0;

    /**
     * @brief Get storage type
     * @return Storage type identifier
     */
    virtual StorageType getType(void) const = 0;

    /**
     * @brief Get total storage capacity in bytes
     * @return Capacity in bytes
     */
    virtual uint32_t getCapacity(void) const = 0;

    /**
     * @brief Check if storage is initialized and ready
     * @return true if ready
     */
    virtual bool isReady(void) const = 0;

    /**
     * @brief Get storage health status
     * @param health Output health structure
     * @return STORAGE_OK on success
     */
    virtual StorageError getHealth(StorageHealth *health) const = 0;

    /**
     * @brief Get error string for error code
     * @param error Error code
     * @return Static error string
     */
    static const char *getErrorString(StorageError error);
};

/**
 * @brief Storage error strings
 */
inline const char *IStorage::getErrorString(StorageError error)
{
    static const char *const ERROR_STRINGS[] = {
        "OK",
        "Null pointer",
        "Invalid address",
        "Invalid size",
        "Not initialized",
        "Write failed",
        "Read failed",
        "Write protected",
        "Device busy",
        "Wear limit reached",
        "Erase failed",
        "Verify failed"
    };

    const int8_t index = (error <= 0) ? static_cast<int8_t>(-error) : 0;
    if (index >= 12) {
        return "Unknown error";
    }
    return ERROR_STRINGS[index];
}

#endif /* STORAGE_INTERFACE_H */
