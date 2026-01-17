/**
 * @file ResilientStorage.cpp
 * @brief Resilient storage implementation with automatic failover
 *
 * Follows NASA's 10 Rules of Safe Code.
 */

#include "ResilientStorage.h"

/**
 * @brief Assertion macro - halts on failure (NASA Rule 5)
 */
#if FM25V02A_DEBUG
#define RESILIENT_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            Serial.printf("RESILIENT ASSERT FAILED: %s:%d - %s\n", __FILE__, __LINE__, #cond); \
            Serial.flush(); \
            while (true) { } \
        } \
    } while (false)
#else
#define RESILIENT_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            while (true) { } \
        } \
    } while (false)
#endif

ResilientStorage::ResilientStorage(FM25V02A *primary, FlashStorage *fallback)
    : m_primary(primary)
    , m_fallback(fallback)
    , m_state(RESILIENT_STATE_PRIMARY)
    , m_consecutiveErrors(0U)
    , m_operationCount(0U)
    , m_lastRecoveryOp(0U)
    , m_recoveryInterval(RESILIENT_RECOVERY_INTERVAL_INITIAL)
    , m_failedRecoveries(0U)
    , m_stats{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
    , m_callback(nullptr)
    , m_callbackContext(nullptr)
    , m_primaryInitialized(false)
    , m_fallbackInitialized(false)
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(primary != nullptr);
    RESILIENT_ASSERT(fallback != nullptr);
}

StorageError ResilientStorage::init(void)
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(m_primary != nullptr);
    RESILIENT_ASSERT(m_fallback != nullptr);

    /* Try to initialize primary (FRAM) */
    FM25V02A_Error framErr = m_primary->init();
    m_primaryInitialized = (framErr == FM25V02A_OK);

    if (!m_primaryInitialized) {
        m_stats.primaryErrors++;
    }

    /* Try to initialize fallback (Flash) */
    StorageError flashErr = m_fallback->init();
    m_fallbackInitialized = (flashErr == STORAGE_OK);

    if (!m_fallbackInitialized) {
        m_stats.fallbackErrors++;
    }

    /* Determine initial state */
    if (m_primaryInitialized) {
        m_state = RESILIENT_STATE_PRIMARY;
    } else if (m_fallbackInitialized) {
        m_state = RESILIENT_STATE_FALLBACK;
        m_stats.failoverCount++;
    } else {
        m_state = RESILIENT_STATE_FAILED;
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    return STORAGE_OK;
}

StorageError ResilientStorage::read(uint32_t address, uint8_t *buffer, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(buffer != nullptr);
    RESILIENT_ASSERT(size > 0U);

    if (buffer == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    if (m_state == RESILIENT_STATE_FAILED) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    m_operationCount++;

    /* Check if should attempt recovery to primary */
    if (shouldAttemptRecovery()) {
        attemptRecovery();
    }

    StorageError err = STORAGE_OK;

    if (m_state == RESILIENT_STATE_PRIMARY) {
        /* Try primary storage first */
        FM25V02A_Error framErr = m_primary->read(
            static_cast<uint16_t>(address), buffer, size);

        if (framErr == FM25V02A_OK) {
            m_stats.primaryReads++;
            m_consecutiveErrors = 0U;
            return STORAGE_OK;
        }

        /* Primary failed - try failover */
        handlePrimaryError(convertFramError(framErr));

        if (m_state == RESILIENT_STATE_FALLBACK) {
            /* Retry with fallback */
            err = m_fallback->read(address, buffer, size);
            if (err == STORAGE_OK) {
                m_stats.fallbackReads++;
            } else {
                handleFallbackError(err);
            }
        }
    } else if (m_state == RESILIENT_STATE_FALLBACK) {
        /* Use fallback storage */
        err = m_fallback->read(address, buffer, size);
        if (err == STORAGE_OK) {
            m_stats.fallbackReads++;
        } else {
            handleFallbackError(err);
        }
    }

    return err;
}

StorageError ResilientStorage::write(uint32_t address, const uint8_t *data, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(data != nullptr);
    RESILIENT_ASSERT(size > 0U);

    if (data == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    if (m_state == RESILIENT_STATE_FAILED) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    m_operationCount++;

    /* Check if should attempt recovery to primary */
    if (shouldAttemptRecovery()) {
        attemptRecovery();
    }

    StorageError err = STORAGE_OK;

    if (m_state == RESILIENT_STATE_PRIMARY) {
        /* Try primary storage first */
        FM25V02A_Error framErr = m_primary->write(
            static_cast<uint16_t>(address), data, size);

        if (framErr == FM25V02A_OK) {
            m_stats.primaryWrites++;
            m_consecutiveErrors = 0U;
            return STORAGE_OK;
        }

        /* Primary failed - try failover */
        handlePrimaryError(convertFramError(framErr));

        if (m_state == RESILIENT_STATE_FALLBACK) {
            /* Retry with fallback */
            err = m_fallback->write(address, data, size);
            if (err == STORAGE_OK) {
                m_stats.fallbackWrites++;
            } else {
                handleFallbackError(err);
            }
        }
    } else if (m_state == RESILIENT_STATE_FALLBACK) {
        /* Use fallback storage */
        err = m_fallback->write(address, data, size);
        if (err == STORAGE_OK) {
            m_stats.fallbackWrites++;
        } else {
            handleFallbackError(err);
        }
    }

    return err;
}

StorageError ResilientStorage::erase(uint32_t address, uint16_t size)
{
    if (m_state == RESILIENT_STATE_FAILED) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* FRAM doesn't need erase, Flash does */
    if (m_state == RESILIENT_STATE_FALLBACK) {
        return m_fallback->erase(address, size);
    }

    /* FRAM: erase is a no-op, just return success */
    return STORAGE_OK;
}

StorageType ResilientStorage::getType(void) const
{
    if (m_state == RESILIENT_STATE_PRIMARY) {
        return STORAGE_TYPE_FRAM;
    } else if (m_state == RESILIENT_STATE_FALLBACK) {
        return STORAGE_TYPE_FLASH;
    }
    return STORAGE_TYPE_UNKNOWN;
}

uint32_t ResilientStorage::getCapacity(void) const
{
    /* Return minimum of both capacities */
    uint32_t primaryCap = FM25V02A_MEMORY_SIZE;
    uint32_t fallbackCap = m_fallback->getCapacity();

    return (primaryCap < fallbackCap) ? primaryCap : fallbackCap;
}

bool ResilientStorage::isReady(void) const
{
    return (m_state != RESILIENT_STATE_FAILED);
}

StorageError ResilientStorage::getHealth(StorageHealth *health) const
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(health != nullptr);

    if (health == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    health->initialized = (m_state != RESILIENT_STATE_FAILED);
    health->healthy = (m_state == RESILIENT_STATE_PRIMARY);
    health->degraded = (m_state == RESILIENT_STATE_FALLBACK);
    health->errorCount = m_stats.primaryErrors + m_stats.fallbackErrors;
    health->writeCount = m_stats.primaryWrites + m_stats.fallbackWrites;

    /* Calculate health percentage */
    if (m_state == RESILIENT_STATE_PRIMARY) {
        health->healthPercent = 100U;
    } else if (m_state == RESILIENT_STATE_FALLBACK) {
        /* Get flash health */
        StorageHealth flashHealth;
        m_fallback->getHealth(&flashHealth);
        health->healthPercent = flashHealth.healthPercent / 2U; /* Degraded */
    } else {
        health->healthPercent = 0U;
    }

    return STORAGE_OK;
}

ResilientState ResilientStorage::getState(void) const
{
    return m_state;
}

StorageError ResilientStorage::getStats(ResilientStats *stats) const
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(stats != nullptr);

    if (stats == nullptr) {
        return STORAGE_ERR_NULL_POINTER;
    }

    *stats = m_stats;
    return STORAGE_OK;
}

StorageError ResilientStorage::forceFailover(void)
{
    if (!m_fallbackInitialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    if (m_state != RESILIENT_STATE_FALLBACK) {
        transitionState(RESILIENT_STATE_FALLBACK);
        m_stats.failoverCount++;
    }

    return STORAGE_OK;
}

StorageError ResilientStorage::attemptRecovery(void)
{
    if (m_state != RESILIENT_STATE_FALLBACK) {
        return STORAGE_OK; /* Already on primary or failed */
    }

    /* Record this recovery attempt */
    m_lastRecoveryOp = m_operationCount;

    transitionState(RESILIENT_STATE_RECOVERING);

    /* Try to re-initialize primary */
    FM25V02A_Error framErr = m_primary->init();

    if (framErr == FM25V02A_OK) {
        /* Test with a read operation */
        uint8_t testByte = 0U;
        framErr = m_primary->readByte(0U, &testByte);

        if (framErr == FM25V02A_OK) {
            /* Recovery successful - reset backoff */
            m_primaryInitialized = true;
            transitionState(RESILIENT_STATE_PRIMARY);
            m_consecutiveErrors = 0U;
            m_failedRecoveries = 0U;
            m_recoveryInterval = RESILIENT_RECOVERY_INTERVAL_INITIAL;
            m_stats.recoveryCount++;
            return STORAGE_OK;
        }
    }

    /* Recovery failed - apply exponential backoff */
    m_failedRecoveries++;
    if (m_recoveryInterval < RESILIENT_RECOVERY_INTERVAL_MAX) {
        m_recoveryInterval *= RESILIENT_RECOVERY_BACKOFF_MULTIPLIER;
        if (m_recoveryInterval > RESILIENT_RECOVERY_INTERVAL_MAX) {
            m_recoveryInterval = RESILIENT_RECOVERY_INTERVAL_MAX;
        }
    }

    /* Stay on fallback */
    transitionState(RESILIENT_STATE_FALLBACK);
    return STORAGE_ERR_READ_FAILED;
}

StorageError ResilientStorage::syncToPrimary(uint32_t address, uint16_t size)
{
    /* NASA Rule 5: Assertions */
    RESILIENT_ASSERT(size <= 256U);

    if (m_state != RESILIENT_STATE_PRIMARY) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    if (!m_fallbackInitialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }

    /* Read from fallback */
    uint8_t buffer[256U];
    const uint16_t clampedSize = (size > 256U) ? 256U : size;

    StorageError err = m_fallback->read(address, buffer, clampedSize);
    if (err != STORAGE_OK) {
        return err;
    }

    /* Write to primary */
    FM25V02A_Error framErr = m_primary->write(
        static_cast<uint16_t>(address), buffer, clampedSize);

    return convertFramError(framErr);
}

void ResilientStorage::setFailoverCallback(ResilientFailoverCallback callback, void *context)
{
    m_callback = callback;
    m_callbackContext = context;
}

bool ResilientStorage::isDegraded(void) const
{
    return (m_state == RESILIENT_STATE_FALLBACK);
}

void ResilientStorage::resetErrorCounters(void)
{
    m_consecutiveErrors = 0U;
    m_stats.primaryErrors = 0U;
    m_stats.fallbackErrors = 0U;
}

void ResilientStorage::handlePrimaryError(StorageError error)
{
    (void)error; /* May be used for logging in future */

    m_stats.primaryErrors++;
    m_consecutiveErrors++;

    /* Check if should failover */
    if (m_consecutiveErrors >= RESILIENT_ERROR_THRESHOLD) {
        if (m_fallbackInitialized) {
            transitionState(RESILIENT_STATE_FALLBACK);
            m_stats.failoverCount++;
            m_consecutiveErrors = 0U;
            /* Reset recovery tracking for fresh start */
            m_lastRecoveryOp = m_operationCount;
            m_recoveryInterval = RESILIENT_RECOVERY_INTERVAL_INITIAL;
            m_failedRecoveries = 0U;
        } else {
            transitionState(RESILIENT_STATE_FAILED);
        }
    }
}

void ResilientStorage::handleFallbackError(StorageError error)
{
    (void)error;

    m_stats.fallbackErrors++;

    /* If fallback fails and primary is also unavailable, we're failed */
    if (!m_primaryInitialized) {
        transitionState(RESILIENT_STATE_FAILED);
    }
}

void ResilientStorage::transitionState(ResilientState newState)
{
    if (m_state != newState) {
        ResilientState oldState = m_state;
        m_state = newState;

        /* Notify callback if registered */
        if (m_callback != nullptr) {
            m_callback(oldState, newState, m_callbackContext);
        }
    }
}

bool ResilientStorage::shouldAttemptRecovery(void) const
{
    /* Only attempt recovery if:
     * 1. Currently on fallback
     * 2. Primary was previously initialized (might just be temporarily unavailable)
     * 3. Enough operations have passed since last recovery attempt
     *    (uses exponential backoff after failed recoveries)
     */
    if ((m_state != RESILIENT_STATE_FALLBACK) || !m_primaryInitialized) {
        return false;
    }

    /* Check if enough operations have passed since last attempt */
    const uint32_t opsSinceLastAttempt = m_operationCount - m_lastRecoveryOp;
    return (opsSinceLastAttempt >= m_recoveryInterval);
}

StorageError ResilientStorage::convertFramError(FM25V02A_Error framError)
{
    switch (framError) {
        case FM25V02A_OK:
            return STORAGE_OK;
        case FM25V02A_ERR_NULL_POINTER:
            return STORAGE_ERR_NULL_POINTER;
        case FM25V02A_ERR_INVALID_ADDRESS:
        case FM25V02A_ERR_ADDRESS_OVERFLOW:
            return STORAGE_ERR_INVALID_ADDRESS;
        case FM25V02A_ERR_INVALID_SIZE:
            return STORAGE_ERR_INVALID_SIZE;
        case FM25V02A_ERR_NOT_INITIALIZED:
            return STORAGE_ERR_NOT_INITIALIZED;
        case FM25V02A_ERR_WRITE_PROTECTED:
            return STORAGE_ERR_WRITE_PROTECTED;
        default:
            return STORAGE_ERR_READ_FAILED;
    }
}
