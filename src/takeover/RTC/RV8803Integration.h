/**
 * @file RV8803Integration.h
 * @brief Integration hooks for RV-8803-C7 with Meshtastic RTC infrastructure
 *
 * This file provides the bridge between the standalone RV8803 driver and
 * the existing Meshtastic RTC system (RTC.h/RTC.cpp).
 *
 * Key integration points:
 * - Device detection via ScanI2C
 * - Time synchronization with quality-based updates
 * - Threshold-based updates to reduce RTC wear
 */

#pragma once

#include "RV8803.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "gps/RTC.h"

// =============================================================================
// Configuration
// =============================================================================

namespace RV8803Integration {

/**
 * @brief Default time difference threshold for RTC updates (5 minutes)
 *
 * Only update the RV8803 if the incoming time differs by at least this many
 * seconds. This reduces wear on the RTC and prevents unnecessary writes
 * when the time is already sufficiently accurate.
 */
static constexpr uint32_t DEFAULT_UPDATE_THRESHOLD_SECS = 300;

/**
 * @brief Minimum quality level required to consider an update
 *
 * Time sources with quality below this level will be ignored entirely.
 */
static constexpr RTCQuality MINIMUM_UPDATE_QUALITY = RTCQualityFromNet;

} // namespace RV8803Integration

// =============================================================================
// Global RV8803 Instance
// =============================================================================

/**
 * @brief Global RV8803 driver instance
 *
 * Initialized by initRV8803() during system startup.
 * Check isRV8803Available() before using.
 */
extern RV8803 *rv8803;

/**
 * @brief Flag indicating if RV8803 was successfully detected and initialized
 */
extern bool rv8803Available;

// =============================================================================
// Initialization Functions
// =============================================================================

/**
 * @brief Initialize RV8803 if detected on I2C bus
 *
 * Should be called during system initialization after I2C scan.
 * This function:
 * 1. Checks if an RV8803 was detected by ScanI2C
 * 2. Creates and initializes the global rv8803 instance
 * 3. Sets rv8803Available flag
 *
 * @param i2cScanner Pointer to the I2C scanner that has completed scanning
 * @return true if RV8803 was found and initialized successfully
 *
 * Example usage in main.cpp:
 * @code
 * #include "takeover/RTC/RV8803Integration.h"
 *
 * void setup() {
 *     // ... I2C scan ...
 *     if (initRV8803(i2cScanner)) {
 *         LOG_INFO("RV8803 RTC initialized");
 *     }
 * }
 * @endcode
 */
bool initRV8803(ScanI2C *i2cScanner);

/**
 * @brief Check if RV8803 is available and initialized
 * @return true if RV8803 can be used
 */
bool isRV8803Available();

// =============================================================================
// Time Synchronization Functions
// =============================================================================

/**
 * @brief Result of a time synchronization attempt
 */
enum class RV8803SyncResult : uint8_t {
    UPDATED = 0,           ///< RTC was updated with new time
    THRESHOLD_NOT_MET = 1, ///< Time difference below threshold, no update
    QUALITY_TOO_LOW = 2,   ///< Source quality insufficient
    RTC_NOT_AVAILABLE = 3, ///< RV8803 not initialized
    ERROR = 4              ///< I2C or other error occurred
};

/**
 * @brief Conditionally update RV8803 based on time delta and quality
 *
 * This is the main integration point for time synchronization. Call this
 * function when receiving time from mesh packets instead of directly
 * calling perhapsSetRTC().
 *
 * The function will:
 * 1. Check if the source quality meets minimum requirements
 * 2. Compare incoming time with current RTC time
 * 3. Only update if the difference exceeds the threshold
 * 4. Log the decision for debugging
 *
 * @param quality Quality level of the time source
 * @param newEpoch New epoch time to potentially set
 * @param forceUpdate If true, bypass threshold check (for GPS/NTP sources)
 * @return RV8803SyncResult indicating what action was taken
 *
 * Example usage in PositionModule.cpp:
 * @code
 * void PositionModule::trySetRtc(meshtastic_Position p, bool isLocal, bool forceUpdate) {
 *     RTCQuality quality = isLocal ? RTCQualityNTP : RTCQualityFromNet;
 *
 *     // Try RV8803 first with threshold check
 *     RV8803SyncResult result = syncRV8803Time(quality, p.time, forceUpdate);
 *
 *     if (result == RV8803SyncResult::RTC_NOT_AVAILABLE) {
 *         // Fall back to standard RTC handling
 *         struct timeval tv = { .tv_sec = p.time, .tv_usec = 0 };
 *         perhapsSetRTC(quality, &tv, forceUpdate);
 *     }
 * }
 * @endcode
 */
RV8803SyncResult syncRV8803Time(RTCQuality quality, uint32_t newEpoch, bool forceUpdate = false);

/**
 * @brief Get time difference between current RTC and a given epoch
 *
 * Useful for diagnostic purposes to see how far off the RTC is.
 *
 * @param compareEpoch Epoch time to compare against
 * @param[out] deltaSeconds Absolute difference in seconds
 * @return RV8803Error::OK on success
 */
RV8803Error getRV8803TimeDelta(uint32_t compareEpoch, uint32_t &deltaSeconds);

/**
 * @brief Read time from RV8803 into Meshtastic RTC system
 *
 * Called during boot to initialize system time from the RTC.
 * This is equivalent to readFromRTC() for the RV8803.
 *
 * @return RTCSetResult indicating success or failure
 */
RTCSetResult readFromRV8803();

// =============================================================================
// Configuration Functions
// =============================================================================

/**
 * @brief Set the time update threshold
 *
 * Changes the minimum time difference required to trigger an RTC update.
 *
 * @param thresholdSecs New threshold in seconds (0 = always update)
 */
void setRV8803UpdateThreshold(uint32_t thresholdSecs);

/**
 * @brief Get the current time update threshold
 * @return Current threshold in seconds
 */
uint32_t getRV8803UpdateThreshold();

// =============================================================================
// Diagnostic Functions
// =============================================================================

/**
 * @brief Get human-readable string for sync result
 * @param result Sync result code
 * @return String description
 */
const char *syncResultToString(RV8803SyncResult result);

/**
 * @brief Print RV8803 status to log
 *
 * Outputs current time, voltage status, and configuration for debugging.
 */
void logRV8803Status();
