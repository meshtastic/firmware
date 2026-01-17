/**
 * @file FM25V02A.h
 * @brief FM25V02A 256-Kbit (32K x 8) Serial SPI F-RAM Driver
 *
 * This driver follows NASA's 10 Rules of Safe Code:
 * 1. Simple control flow (no goto, setjmp, recursion)
 * 2. Fixed upper bound on all loops (max 256 bytes per operation)
 * 3. No dynamic memory allocation after initialization
 * 4. Functions limited to ~60 lines
 * 5. Minimum 2 assertions per function
 * 6. Data declared at smallest scope
 * 7. All return values checked, all parameters validated
 * 8. Limited preprocessor use (includes and simple macros only)
 * 9. Restricted pointer use
 * 10. Compiled with all warnings enabled
 *
 * @see https://www.infineon.com/dgdl/Infineon-FM25V02A-DataSheet
 */

#ifndef FM25V02A_H
#define FM25V02A_H

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

/**
 * @brief Maximum single transfer size in bytes (NASA Rule 2: bounded loops)
 */
#define FM25V02A_MAX_TRANSFER_SIZE 256U

/**
 * @brief Total memory size in bytes (32K x 8 = 32768 bytes)
 */
#define FM25V02A_MEMORY_SIZE 32768U

/**
 * @brief Maximum valid memory address
 */
#define FM25V02A_MAX_ADDRESS (FM25V02A_MEMORY_SIZE - 1U)

/**
 * @brief Address requires 16 bits (15 bits used for 32KB)
 */
#define FM25V02A_ADDRESS_BYTES 2U

/**
 * @brief Manufacturer ID (Cypress/Infineon in JEDEC bank 7)
 */
#define FM25V02A_MANUFACTURER_ID 0x7F7F7F7F7F7FC2U

/**
 * @brief Expected product ID
 */
#define FM25V02A_PRODUCT_ID 0x2200U

/**
 * @brief CRC16 polynomial (CRC-16-CCITT)
 */
#define FM25V02A_CRC16_POLY 0x1021U

/**
 * @brief CRC16 initial value
 */
#define FM25V02A_CRC16_INIT 0xFFFFU

/**
 * @brief Error codes returned by FM25V02A operations
 */
typedef enum {
    FM25V02A_OK = 0,                    /**< Operation successful */
    FM25V02A_ERR_NULL_POINTER = -1,     /**< Null pointer passed */
    FM25V02A_ERR_INVALID_ADDRESS = -2,  /**< Address out of range */
    FM25V02A_ERR_INVALID_SIZE = -3,     /**< Size is zero or exceeds limit */
    FM25V02A_ERR_ADDRESS_OVERFLOW = -4, /**< Address + size exceeds memory */
    FM25V02A_ERR_NOT_INITIALIZED = -5,  /**< Device not initialized */
    FM25V02A_ERR_DEVICE_ID = -6,        /**< Device ID mismatch */
    FM25V02A_ERR_WRITE_ENABLE = -7,     /**< Failed to enable writes */
    FM25V02A_ERR_CRC_MISMATCH = -8,     /**< CRC verification failed */
    FM25V02A_ERR_SPI_NULL = -9,         /**< SPI bus pointer is null */
    FM25V02A_ERR_ASSERTION = -10,       /**< Assertion failure */
    FM25V02A_ERR_ASLEEP = -11,          /**< Device is in sleep mode */
    FM25V02A_ERR_WRITE_PROTECTED = -12  /**< Memory region is write protected */
} FM25V02A_Error;

/**
 * @brief Status register bit definitions
 */
typedef enum {
    FM25V02A_STATUS_WEL = 0x02U,   /**< Write Enable Latch (bit 1) */
    FM25V02A_STATUS_BP0 = 0x04U,   /**< Block Protect 0 (bit 2) */
    FM25V02A_STATUS_BP1 = 0x08U,   /**< Block Protect 1 (bit 3) */
    FM25V02A_STATUS_WPEN = 0x80U   /**< Write Protect Enable (bit 7) */
} FM25V02A_StatusBits;

/**
 * @brief Write protection levels
 */
typedef enum {
    FM25V02A_PROTECT_NONE = 0x00U,      /**< No protection */
    FM25V02A_PROTECT_UPPER_QUARTER = 0x04U,  /**< Protect 0x6000-0x7FFF */
    FM25V02A_PROTECT_UPPER_HALF = 0x08U,     /**< Protect 0x4000-0x7FFF */
    FM25V02A_PROTECT_ALL = 0x0CU        /**< Protect all memory */
} FM25V02A_Protection;

/**
 * @brief Callback function type for error notification
 *
 * @param error The error code that occurred
 * @param address The memory address involved (if applicable)
 * @param context User-provided context pointer
 */
typedef void (*FM25V02A_ErrorCallback)(FM25V02A_Error error, uint16_t address, void *context);

/**
 * @brief Device state tracking
 */
typedef struct {
    bool initialized;   /**< True if device successfully initialized */
    bool asleep;        /**< True if device is in sleep mode */
    uint8_t status;     /**< Last read status register value */
} FM25V02A_State;

/**
 * @brief FM25V02A FRAM Driver Class
 *
 * Provides complete control of the FM25V02A 256-Kbit SPI FRAM with
 * NASA-compliant safety features including parameter validation,
 * assertions, CRC verification, and bounded operations.
 */
class FM25V02A {
public:
    /**
     * @brief Construct FM25V02A driver instance
     *
     * @param spi Pointer to SPI bus instance (must not be null)
     * @param csPin Chip select pin number
     * @param spiSpeed SPI clock speed in Hz (max 40MHz)
     *
     * @note NASA Rule 3: No dynamic allocation, uses provided SPI instance
     */
    FM25V02A(SPIClass *spi, uint8_t csPin, uint32_t spiSpeed = 20000000U);

    /**
     * @brief Initialize the FRAM device
     *
     * Verifies device presence by reading and validating device ID.
     * Must be called before any other operations.
     *
     * @return FM25V02A_OK on success, error code otherwise
     *
     * @note NASA Rule 7: Return value must be checked by caller
     */
    FM25V02A_Error init(void);

    /**
     * @brief Read data from FRAM
     *
     * @param address Start address (0x0000 to 0x7FFF)
     * @param buffer Destination buffer (must not be null)
     * @param size Number of bytes to read (1 to FM25V02A_MAX_TRANSFER_SIZE)
     *
     * @return FM25V02A_OK on success, error code otherwise
     *
     * @note NASA Rule 2: Size bounded to FM25V02A_MAX_TRANSFER_SIZE
     * @note NASA Rule 7: All parameters validated, return must be checked
     */
    FM25V02A_Error read(uint16_t address, uint8_t *buffer, uint16_t size);

    /**
     * @brief Write data to FRAM
     *
     * @param address Start address (0x0000 to 0x7FFF)
     * @param data Source data buffer (must not be null)
     * @param size Number of bytes to write (1 to FM25V02A_MAX_TRANSFER_SIZE)
     *
     * @return FM25V02A_OK on success, error code otherwise
     *
     * @note NASA Rule 2: Size bounded to FM25V02A_MAX_TRANSFER_SIZE
     * @note NASA Rule 7: All parameters validated, return must be checked
     */
    FM25V02A_Error write(uint16_t address, const uint8_t *data, uint16_t size);

    /**
     * @brief Read data with CRC16 verification
     *
     * Reads data and verifies against stored CRC16 checksum.
     * CRC is expected to be stored in the 2 bytes following the data.
     *
     * @param address Start address of data
     * @param buffer Destination buffer
     * @param size Number of data bytes (CRC stored at address+size)
     *
     * @return FM25V02A_OK on success, FM25V02A_ERR_CRC_MISMATCH if CRC fails
     */
    FM25V02A_Error readWithCRC(uint16_t address, uint8_t *buffer, uint16_t size);

    /**
     * @brief Write data with CRC16 appended
     *
     * Writes data followed by computed CRC16 checksum (2 bytes).
     * Total bytes written = size + 2.
     *
     * @param address Start address for data
     * @param data Source data buffer
     * @param size Number of data bytes
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error writeWithCRC(uint16_t address, const uint8_t *data, uint16_t size);

    /**
     * @brief Read a single byte from FRAM
     *
     * @param address Memory address
     * @param value Pointer to store read value
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error readByte(uint16_t address, uint8_t *value);

    /**
     * @brief Write a single byte to FRAM
     *
     * @param address Memory address
     * @param value Byte value to write
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error writeByte(uint16_t address, uint8_t value);

    /**
     * @brief Read a 16-bit value (big-endian)
     *
     * @param address Memory address
     * @param value Pointer to store read value
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error readUint16(uint16_t address, uint16_t *value);

    /**
     * @brief Write a 16-bit value (big-endian)
     *
     * @param address Memory address
     * @param value Value to write
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error writeUint16(uint16_t address, uint16_t value);

    /**
     * @brief Read a 32-bit value (big-endian)
     *
     * @param address Memory address
     * @param value Pointer to store read value
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error readUint32(uint16_t address, uint32_t *value);

    /**
     * @brief Write a 32-bit value (big-endian)
     *
     * @param address Memory address
     * @param value Value to write
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error writeUint32(uint16_t address, uint32_t value);

    /**
     * @brief Enter low-power sleep mode
     *
     * In sleep mode, all operations except wake() will return FM25V02A_ERR_ASLEEP.
     * Current consumption drops to ~4uA typical.
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error sleep(void);

    /**
     * @brief Wake from sleep mode
     *
     * Device is ready for operations after wake returns.
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error wake(void);

    /**
     * @brief Check if device is in sleep mode
     *
     * @return true if device is asleep
     */
    bool isAsleep(void) const;

    /**
     * @brief Read the status register
     *
     * @param status Pointer to store status register value
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error readStatus(uint8_t *status);

    /**
     * @brief Set write protection level
     *
     * @param protection Protection level to set
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error setProtection(FM25V02A_Protection protection);

    /**
     * @brief Get current write protection level
     *
     * @param protection Pointer to store current protection level
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error getProtection(FM25V02A_Protection *protection);

    /**
     * @brief Read device ID
     *
     * @param manufacturerId Pointer to store manufacturer ID (optional, can be null)
     * @param productId Pointer to store product ID (optional, can be null)
     *
     * @return FM25V02A_OK on success, error code otherwise
     */
    FM25V02A_Error readDeviceId(uint32_t *manufacturerId, uint16_t *productId);

    /**
     * @brief Check if device is initialized
     *
     * @return true if init() completed successfully
     */
    bool isInitialized(void) const;

    /**
     * @brief Register error callback
     *
     * @param callback Function to call on errors (null to disable)
     * @param context User context passed to callback
     *
     * @note NASA Rule 3: Context must be valid for lifetime of driver
     */
    void setErrorCallback(FM25V02A_ErrorCallback callback, void *context);

    /**
     * @brief Calculate CRC16 for data buffer
     *
     * Uses CRC-16-CCITT polynomial (0x1021).
     *
     * @param data Data buffer
     * @param size Number of bytes
     *
     * @return Computed CRC16 value
     */
    static uint16_t calculateCRC16(const uint8_t *data, uint16_t size);

    /**
     * @brief Get error description string
     *
     * @param error Error code
     *
     * @return Pointer to static string describing the error
     */
    static const char *getErrorString(FM25V02A_Error error);

private:
    /**
     * @brief SPI command opcodes
     */
    enum Opcode : uint8_t {
        OPCODE_WREN  = 0x06U,  /**< Write Enable */
        OPCODE_WRDI  = 0x04U,  /**< Write Disable */
        OPCODE_RDSR  = 0x05U,  /**< Read Status Register */
        OPCODE_WRSR  = 0x01U,  /**< Write Status Register */
        OPCODE_READ  = 0x03U,  /**< Read Memory */
        OPCODE_WRITE = 0x02U,  /**< Write Memory */
        OPCODE_SLEEP = 0xB9U,  /**< Enter Sleep Mode */
        OPCODE_RDID  = 0x9FU   /**< Read Device ID */
    };

    /**
     * @brief Enable write operations
     *
     * Must be called before any write operation.
     * Verifies WEL bit is set after command.
     *
     * @return FM25V02A_OK if write enabled, error code otherwise
     */
    FM25V02A_Error writeEnable(void);

    /**
     * @brief Disable write operations
     *
     * @return FM25V02A_OK on success
     */
    FM25V02A_Error writeDisable(void);

    /**
     * @brief Begin SPI transaction
     *
     * Acquires SPI bus and asserts CS.
     */
    void beginTransaction(void);

    /**
     * @brief End SPI transaction
     *
     * Deasserts CS and releases SPI bus.
     */
    void endTransaction(void);

    /**
     * @brief Send single opcode command
     *
     * @param opcode Command opcode
     */
    void sendOpcode(uint8_t opcode);

    /**
     * @brief Validate address and size parameters
     *
     * @param address Start address
     * @param size Transfer size
     *
     * @return FM25V02A_OK if valid, error code otherwise
     */
    FM25V02A_Error validateAddressAndSize(uint16_t address, uint16_t size) const;

    /**
     * @brief Check if address range is write protected
     *
     * @param address Start address
     * @param size Transfer size
     *
     * @return true if any part of range is protected
     */
    bool isWriteProtected(uint16_t address, uint16_t size) const;

    /**
     * @brief Report error via callback if registered
     *
     * @param error Error code
     * @param address Associated address
     */
    void reportError(FM25V02A_Error error, uint16_t address);

    /**
     * @brief Assertion macro for NASA Rule 5
     *
     * Halts system on failure in debug mode.
     *
     * @param condition Condition that must be true
     * @param error Error to return if condition is false
     */
    FM25V02A_Error assertCondition(bool condition, FM25V02A_Error error);

    SPIClass *m_spi;                        /**< SPI bus instance */
    SPISettings m_spiSettings;              /**< SPI configuration */
    uint8_t m_csPin;                        /**< Chip select pin */
    FM25V02A_State m_state;                 /**< Device state */
    FM25V02A_ErrorCallback m_errorCallback; /**< Error notification callback */
    void *m_errorContext;                   /**< User context for callback */
};

#endif /* FM25V02A_H */
