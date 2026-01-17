/**
 * @file ResilientStorage.h
 * @brief Resilient storage with automatic FRAM-to-Flash failover
 *
 * Provides transparent failover from FRAM to Flash memory when errors
 * occur, ensuring data persistence even when primary storage fails.
 *
 * Follows NASA's 10 Rules of Safe Code.
 */

#ifndef RESILIENT_STORAGE_H
#define RESILIENT_STORAGE_H

#include "StorageInterface.h"
#include "FM25V02A.h"
#include "FlashStorage.h"

/**
 * @brief Maximum consecutive errors before failover
 */
#define RESILIENT_ERROR_THRESHOLD 3U

/**
 * @brief Recovery check interval (number of operations)
 */
#define RESILIENT_RECOVERY_INTERVAL 100U

/**
 * @brief Resilient storage state
 */
typedef enum {
    RESILIENT_STATE_PRIMARY = 0,     /**< Using primary (FRAM) storage */
    RESILIENT_STATE_FALLBACK = 1,    /**< Using fallback (Flash) storage */
    RESILIENT_STATE_RECOVERING = 2,  /**< Attempting recovery to primary */
    RESILIENT_STATE_FAILED = 3       /**< Both storages failed */
} ResilientState;

/**
 * @brief Failover event callback
 */
typedef void (*ResilientFailoverCallback)(ResilientState oldState,
                                          ResilientState newState,
                                          void *context);

/**
 * @brief Resilient storage statistics
 */
typedef struct {
    uint32_t primaryReads;       /**< Successful primary reads */
    uint32_t primaryWrites;      /**< Successful primary writes */
    uint32_t fallbackReads;      /**< Successful fallback reads */
    uint32_t fallbackWrites;     /**< Successful fallback writes */
    uint32_t primaryErrors;      /**< Primary storage errors */
    uint32_t fallbackErrors;     /**< Fallback storage errors */
    uint32_t failoverCount;      /**< Number of failovers */
    uint32_t recoveryCount;      /**< Successful recoveries */
} ResilientStats;

/**
 * @brief Resilient Storage Class
 *
 * Wraps primary (FRAM) and fallback (Flash) storage backends,
 * automatically switching to fallback when primary fails.
 */
class ResilientStorage : public IStorage {
public:
    /**
     * @brief Construct ResilientStorage
     *
     * @param primary Primary storage backend (FRAM)
     * @param fallback Fallback storage backend (Flash)
     */
    ResilientStorage(FM25V02A *primary, FlashStorage *fallback);

    /**
     * @brief Initialize both storage backends
     * @return STORAGE_OK if at least one backend initialized
     */
    StorageError init(void) override;

    /**
     * @brief Read data with automatic failover
     * @param address Starting address
     * @param buffer Buffer to store read data
     * @param size Number of bytes to read
     * @return STORAGE_OK on success
     */
    StorageError read(uint32_t address, uint8_t *buffer, uint16_t size) override;

    /**
     * @brief Write data with automatic failover
     * @param address Starting address
     * @param data Data to write
     * @param size Number of bytes to write
     * @return STORAGE_OK on success
     */
    StorageError write(uint32_t address, const uint8_t *data, uint16_t size) override;

    /**
     * @brief Erase storage region
     * @param address Starting address
     * @param size Number of bytes to erase
     * @return STORAGE_OK on success
     */
    StorageError erase(uint32_t address, uint16_t size) override;

    /**
     * @brief Get active storage type
     * @return Currently active storage type
     */
    StorageType getType(void) const override;

    /**
     * @brief Get storage capacity
     * @return Minimum capacity of both backends
     */
    uint32_t getCapacity(void) const override;

    /**
     * @brief Check if storage is ready
     * @return true if at least one backend is ready
     */
    bool isReady(void) const override;

    /**
     * @brief Get combined health status
     * @param health Output health structure
     * @return STORAGE_OK on success
     */
    StorageError getHealth(StorageHealth *health) const override;

    /**
     * @brief Get current resilient state
     * @return Current state
     */
    ResilientState getState(void) const;

    /**
     * @brief Get statistics
     * @param stats Output statistics structure
     * @return STORAGE_OK on success
     */
    StorageError getStats(ResilientStats *stats) const;

    /**
     * @brief Force failover to fallback storage
     * @return STORAGE_OK on success
     */
    StorageError forceFailover(void);

    /**
     * @brief Attempt recovery to primary storage
     * @return STORAGE_OK if recovery successful
     */
    StorageError attemptRecovery(void);

    /**
     * @brief Synchronize data from fallback to primary
     *
     * Copies data from fallback storage back to primary after
     * primary recovery. Call this to restore data consistency.
     *
     * @param address Starting address
     * @param size Number of bytes to sync
     * @return STORAGE_OK on success
     */
    StorageError syncToPrimary(uint32_t address, uint16_t size);

    /**
     * @brief Set failover event callback
     * @param callback Callback function
     * @param context User context pointer
     */
    void setFailoverCallback(ResilientFailoverCallback callback, void *context);

    /**
     * @brief Check if operating in degraded mode
     * @return true if using fallback storage
     */
    bool isDegraded(void) const;

    /**
     * @brief Reset error counters (e.g., after maintenance)
     */
    void resetErrorCounters(void);

private:
    /**
     * @brief Handle error from primary storage
     * @param error Error code
     */
    void handlePrimaryError(StorageError error);

    /**
     * @brief Handle error from fallback storage
     * @param error Error code
     */
    void handleFallbackError(StorageError error);

    /**
     * @brief Transition to new state
     * @param newState Target state
     */
    void transitionState(ResilientState newState);

    /**
     * @brief Check if should attempt automatic recovery
     * @return true if recovery should be attempted
     */
    bool shouldAttemptRecovery(void) const;

    /**
     * @brief Convert FM25V02A error to StorageError
     * @param framError FRAM error code
     * @return Storage error code
     */
    static StorageError convertFramError(FM25V02A_Error framError);

    FM25V02A *m_primary;                /**< Primary storage (FRAM) */
    FlashStorage *m_fallback;           /**< Fallback storage (Flash) */
    ResilientState m_state;             /**< Current state */
    uint8_t m_consecutiveErrors;        /**< Consecutive error count */
    uint32_t m_operationCount;          /**< Operations since last recovery check */
    ResilientStats m_stats;             /**< Statistics */
    ResilientFailoverCallback m_callback; /**< Failover callback */
    void *m_callbackContext;            /**< Callback context */
    bool m_primaryInitialized;          /**< Primary init success */
    bool m_fallbackInitialized;         /**< Fallback init success */
};

#endif /* RESILIENT_STORAGE_H */
