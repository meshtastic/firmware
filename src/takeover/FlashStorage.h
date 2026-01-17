/**
 * @file FlashStorage.h
 * @brief Flash memory storage backend for graceful FRAM fallback
 *
 * Provides flash memory storage as a fallback when FRAM fails.
 * Implements wear leveling and sector management for extended lifetime.
 *
 * Follows NASA's 10 Rules of Safe Code.
 */

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include "StorageInterface.h"
#include <Arduino.h>
#include <SPI.h>

/**
 * @brief Flash memory constants
 */
#define FLASH_SECTOR_SIZE       4096U       /**< 4KB sector size */
#define FLASH_PAGE_SIZE         256U        /**< 256-byte page size */
#define FLASH_MAX_ADDRESS       0x3FFFFFU   /**< 4MB max (W25Q32) */
#define FLASH_MAX_TRANSFER_SIZE 256U        /**< Max single transfer */

/**
 * @brief Flash operation timeouts (microseconds)
 */
#define FLASH_PAGE_PROGRAM_TIMEOUT_US   3000U   /**< 3ms max page program */
#define FLASH_SECTOR_ERASE_TIMEOUT_US   400000U /**< 400ms max sector erase */
#define FLASH_CHIP_ERASE_TIMEOUT_US     100000000U /**< 100s max chip erase */

/**
 * @brief Flash status register bits
 */
#define FLASH_STATUS_BUSY       0x01U
#define FLASH_STATUS_WEL        0x02U

/**
 * @brief Wear tracking structure (stored at end of each sector)
 */
typedef struct {
    uint32_t magic;             /**< Validation magic (0x57454152 = "WEAR") */
    uint32_t eraseCount;        /**< Number of times sector has been erased */
    uint16_t crc;               /**< CRC16 of wear data */
} FlashWearInfo;

#define FLASH_WEAR_MAGIC        0x57454152U
#define FLASH_WEAR_INFO_SIZE    10U

/**
 * @brief Flash Storage Class
 *
 * Provides flash memory storage with wear leveling support.
 * Designed as a fallback for FRAM when it fails.
 */
class FlashStorage : public IStorage {
public:
    /**
     * @brief Construct FlashStorage instance
     *
     * @param spi SPI bus instance
     * @param csPin Chip select pin
     * @param spiSpeed SPI clock speed in Hz (max 80MHz for most flash)
     * @param baseAddress Base address for this storage region
     * @param size Size of storage region in bytes
     */
    FlashStorage(SPIClass *spi, uint8_t csPin, uint32_t spiSpeed,
                 uint32_t baseAddress, uint32_t size);

    /**
     * @brief Initialize flash storage
     * @return STORAGE_OK on success
     */
    StorageError init(void) override;

    /**
     * @brief Read data from flash
     * @param address Starting address
     * @param buffer Buffer to store read data
     * @param size Number of bytes to read
     * @return STORAGE_OK on success
     */
    StorageError read(uint32_t address, uint8_t *buffer, uint16_t size) override;

    /**
     * @brief Write data to flash
     * @param address Starting address
     * @param data Data to write
     * @param size Number of bytes to write
     * @return STORAGE_OK on success
     */
    StorageError write(uint32_t address, const uint8_t *data, uint16_t size) override;

    /**
     * @brief Erase flash sector(s)
     * @param address Starting address (aligned to sector)
     * @param size Number of bytes to erase
     * @return STORAGE_OK on success
     */
    StorageError erase(uint32_t address, uint16_t size) override;

    /**
     * @brief Get storage type
     * @return STORAGE_TYPE_FLASH
     */
    StorageType getType(void) const override;

    /**
     * @brief Get total storage capacity
     * @return Capacity in bytes
     */
    uint32_t getCapacity(void) const override;

    /**
     * @brief Check if storage is ready
     * @return true if initialized and ready
     */
    bool isReady(void) const override;

    /**
     * @brief Get storage health status
     * @param health Output health structure
     * @return STORAGE_OK on success
     */
    StorageError getHealth(StorageHealth *health) const override;

    /**
     * @brief Read flash device ID
     * @param manufacturerId Output manufacturer ID
     * @param deviceId Output device ID
     * @return STORAGE_OK on success
     */
    StorageError readDeviceId(uint8_t *manufacturerId, uint16_t *deviceId);

    /**
     * @brief Get estimated remaining write cycles
     * @return Estimated remaining cycles (0 = end of life)
     */
    uint32_t getRemainingCycles(void) const;

private:
    /* SPI Flash opcodes */
    static const uint8_t OPCODE_READ = 0x03U;
    static const uint8_t OPCODE_FAST_READ = 0x0BU;
    static const uint8_t OPCODE_PAGE_PROGRAM = 0x02U;
    static const uint8_t OPCODE_SECTOR_ERASE = 0x20U;
    static const uint8_t OPCODE_BLOCK_ERASE_32K = 0x52U;
    static const uint8_t OPCODE_BLOCK_ERASE_64K = 0xD8U;
    static const uint8_t OPCODE_CHIP_ERASE = 0xC7U;
    static const uint8_t OPCODE_WRITE_ENABLE = 0x06U;
    static const uint8_t OPCODE_WRITE_DISABLE = 0x04U;
    static const uint8_t OPCODE_READ_STATUS = 0x05U;
    static const uint8_t OPCODE_WRITE_STATUS = 0x01U;
    static const uint8_t OPCODE_READ_ID = 0x9FU;
    static const uint8_t OPCODE_POWER_DOWN = 0xB9U;
    static const uint8_t OPCODE_RELEASE_POWER_DOWN = 0xABU;

    /**
     * @brief Begin SPI transaction
     */
    void beginTransaction(void);

    /**
     * @brief End SPI transaction
     */
    void endTransaction(void);

    /**
     * @brief Enable write operations
     * @return STORAGE_OK on success
     */
    StorageError writeEnable(void);

    /**
     * @brief Wait for flash to complete operation
     * @param timeoutUs Timeout in microseconds
     * @return STORAGE_OK if ready, error if timeout
     */
    StorageError waitReady(uint32_t timeoutUs);

    /**
     * @brief Read status register
     * @return Status register value
     */
    uint8_t readStatus(void);

    /**
     * @brief Write a single page (up to 256 bytes)
     * @param address Page-aligned address
     * @param data Data to write
     * @param size Number of bytes (max 256)
     * @return STORAGE_OK on success
     */
    StorageError writePage(uint32_t address, const uint8_t *data, uint16_t size);

    /**
     * @brief Erase a single sector
     * @param sectorAddress Sector-aligned address
     * @return STORAGE_OK on success
     */
    StorageError eraseSector(uint32_t sectorAddress);

    /**
     * @brief Update wear info for a sector
     * @param sectorAddress Sector address
     * @return STORAGE_OK on success
     */
    StorageError updateWearInfo(uint32_t sectorAddress);

    /**
     * @brief Read wear info for a sector
     * @param sectorAddress Sector address
     * @param info Output wear info
     * @return STORAGE_OK on success
     */
    StorageError readWearInfo(uint32_t sectorAddress, FlashWearInfo *info);

    /**
     * @brief Calculate CRC16 for data
     * @param data Data buffer
     * @param size Data size
     * @return CRC16 value
     */
    static uint16_t calculateCRC16(const uint8_t *data, uint16_t size);

    SPIClass *m_spi;                /**< SPI bus instance */
    SPISettings m_spiSettings;      /**< SPI configuration */
    uint8_t m_csPin;                /**< Chip select pin */
    uint32_t m_baseAddress;         /**< Base address for region */
    uint32_t m_size;                /**< Size of storage region */
    bool m_initialized;             /**< Initialization state */
    uint32_t m_errorCount;          /**< Cumulative error count */
    uint32_t m_writeCount;          /**< Total write operations */
    uint32_t m_maxEraseCount;       /**< Highest sector erase count */
};

#endif /* FLASH_STORAGE_H */
