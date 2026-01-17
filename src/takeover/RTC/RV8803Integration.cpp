/**
 * @file RV8803Integration.cpp
 * @brief Implementation of RV-8803-C7 integration with Meshtastic RTC system
 *
 * Following NASA's Power of 10 Rules for Safety-Critical Code.
 */

#include "RV8803Integration.h"
#include "detect/ScanI2CTwoWire.h"

// =============================================================================
// Global Instance
// =============================================================================

RV8803 *rv8803 = nullptr;
bool rv8803Available = false;

// =============================================================================
// Initialization Functions
// =============================================================================

bool initRV8803(ScanI2C *i2cScanner)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(i2cScanner != nullptr);

    if (i2cScanner == nullptr) {
        LOG_WARN("RV8803: Cannot init - i2cScanner is null");
        return false;
    }

    // Check if RV8803 was detected during I2C scan
    // Note: You may need to add RTC_RV8803 to ScanI2C::DeviceType enum
    ScanI2C::FoundDevice dev = i2cScanner->find(ScanI2C::DeviceType::RTC_RV3028);

    // If not found as RV3028, check at the RV8803 specific address
    if (dev.type == ScanI2C::DeviceType::NONE) {
        // Manual check at RV8803 address (0x32)
        // The RV8803 may not be in the device list yet
        LOG_DEBUG("RV8803: Not found in I2C scan, checking address 0x32 manually");

        // For now, we'll try to initialize at the known address
        // In production, add RTC_RV8803 to ScanI2C::DeviceType
    }

    // Create instance with default threshold (5 minutes)
    rv8803 = new RV8803(RV8803Integration::DEFAULT_UPDATE_THRESHOLD_SECS);

    if (rv8803 == nullptr) {
        LOG_ERROR("RV8803: Failed to allocate instance");
        return false;
    }

// Determine which I2C bus to use
#if WIRE_INTERFACES_COUNT > 1
    TwoWire *bus = (dev.address.port == ScanI2C::I2CPort::WIRE1) ? &Wire1 : &Wire;
#else
    TwoWire *bus = &Wire;
#endif

    // Initialize the device
    RV8803Error err = rv8803->begin(*bus, RV8803_I2C_ADDR);

    if (err != RV8803Error::OK) {
        LOG_WARN("RV8803: Initialization failed - %s", RV8803::errorToString(err));
        delete rv8803;
        rv8803 = nullptr;
        rv8803Available = false;
        return false;
    }

    // Check voltage status
    err = rv8803->checkVoltage();
    if (err == RV8803Error::VOLTAGE_LOW) {
        LOG_WARN("RV8803: Low backup voltage detected - time may be invalid");
        // Continue anyway, but caller should be aware
    }

    rv8803Available = true;
    LOG_INFO("RV8803: Initialized successfully with %lu sec update threshold", RV8803Integration::DEFAULT_UPDATE_THRESHOLD_SECS);

    return true;
}

bool isRV8803Available()
{
    return rv8803Available && (rv8803 != nullptr) && rv8803->isInitialized();
}

// =============================================================================
// Time Synchronization Functions
// =============================================================================

RV8803SyncResult syncRV8803Time(RTCQuality quality, uint32_t newEpoch, bool forceUpdate)
{
    // NASA Rule #5: Assertions
    RV8803_ASSERT(newEpoch > 0);

    // Check if RV8803 is available
    if (!isRV8803Available()) {
        return RV8803SyncResult::RTC_NOT_AVAILABLE;
    }

    // Check minimum quality requirement (unless forced)
    if (!forceUpdate && quality < RV8803Integration::MINIMUM_UPDATE_QUALITY) {
        LOG_DEBUG("RV8803: Ignoring time update - quality %d below minimum %d", quality,
                  RV8803Integration::MINIMUM_UPDATE_QUALITY);
        return RV8803SyncResult::QUALITY_TOO_LOW;
    }

    // For high-quality sources (GPS, NTP), always update
    if (forceUpdate || quality >= RTCQualityNTP) {
        LOG_INFO("RV8803: Force updating time from %s source", RtcName(quality));

        RV8803Error err = rv8803->setEpoch(newEpoch);
        if (err != RV8803Error::OK) {
            LOG_ERROR("RV8803: Failed to set time - %s", RV8803::errorToString(err));
            return RV8803SyncResult::ERROR;
        }

        return RV8803SyncResult::UPDATED;
    }

    // For mesh-sourced time, use threshold-based update
    uint32_t deltaSeconds = 0;
    RV8803Error err = rv8803->updateIfDelta(newEpoch, deltaSeconds);

    switch (err) {
    case RV8803Error::OK:
        LOG_INFO("RV8803: Updated time from %s (delta was %lu sec)", RtcName(quality), deltaSeconds);
        return RV8803SyncResult::UPDATED;

    case RV8803Error::THRESHOLD_NOT_MET:
        LOG_DEBUG("RV8803: Skipped update - delta %lu sec below %lu sec threshold", deltaSeconds,
                  rv8803->getUpdateThreshold());
        return RV8803SyncResult::THRESHOLD_NOT_MET;

    default:
        LOG_ERROR("RV8803: Update failed - %s", RV8803::errorToString(err));
        return RV8803SyncResult::ERROR;
    }
}

RV8803Error getRV8803TimeDelta(uint32_t compareEpoch, uint32_t &deltaSeconds)
{
    // NASA Rule #5: Assertions
    RV8803_ASSERT(compareEpoch > 0);

    if (!isRV8803Available()) {
        return RV8803Error::NOT_INITIALIZED;
    }

    uint32_t currentEpoch = 0;
    RV8803Error err = rv8803->getEpoch(currentEpoch);

    if (err != RV8803Error::OK) {
        return err;
    }

    // Calculate absolute difference
    if (compareEpoch >= currentEpoch) {
        deltaSeconds = compareEpoch - currentEpoch;
    } else {
        deltaSeconds = currentEpoch - compareEpoch;
    }

    return RV8803Error::OK;
}

RTCSetResult readFromRV8803()
{
    if (!isRV8803Available()) {
        return RTCSetResultNotSet;
    }

    uint32_t epoch = 0;
    RV8803Error err = rv8803->getEpoch(epoch);

    if (err != RV8803Error::OK) {
        LOG_WARN("RV8803: Failed to read time - %s", RV8803::errorToString(err));
        return RTCSetResultError;
    }

    // Validate the time is reasonable
    if (epoch < 946684800UL) { // Before year 2000
        LOG_WARN("RV8803: Time before year 2000, may be invalid");
        return RTCSetResultInvalidTime;
    }

#ifdef BUILD_EPOCH
    if (epoch < BUILD_EPOCH) {
        LOG_WARN("RV8803: Time before build epoch, may be invalid");
        return RTCSetResultInvalidTime;
    }
#endif

    // Set system time from RTC
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;

    // Use perhapsSetRTC with Device quality to update system time
    RTCSetResult result = perhapsSetRTC(RTCQualityDevice, &tv, false);

    if (result == RTCSetResultSuccess) {
        LOG_INFO("RV8803: System time set from RTC: %lu", epoch);
    }

    return result;
}

// =============================================================================
// Configuration Functions
// =============================================================================

void setRV8803UpdateThreshold(uint32_t thresholdSecs)
{
    if (rv8803 != nullptr) {
        rv8803->setUpdateThreshold(thresholdSecs);
        LOG_INFO("RV8803: Update threshold set to %lu seconds", thresholdSecs);
    }
}

uint32_t getRV8803UpdateThreshold()
{
    if (rv8803 != nullptr) {
        return rv8803->getUpdateThreshold();
    }
    return RV8803Integration::DEFAULT_UPDATE_THRESHOLD_SECS;
}

// =============================================================================
// Diagnostic Functions
// =============================================================================

const char *syncResultToString(RV8803SyncResult result)
{
    switch (result) {
    case RV8803SyncResult::UPDATED:
        return "UPDATED";
    case RV8803SyncResult::THRESHOLD_NOT_MET:
        return "THRESHOLD_NOT_MET";
    case RV8803SyncResult::QUALITY_TOO_LOW:
        return "QUALITY_TOO_LOW";
    case RV8803SyncResult::RTC_NOT_AVAILABLE:
        return "RTC_NOT_AVAILABLE";
    case RV8803SyncResult::ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void logRV8803Status()
{
    if (!isRV8803Available()) {
        LOG_INFO("RV8803: Not available");
        return;
    }

    // Get current time
    RV8803DateTime dt;
    RV8803Error err = rv8803->getDateTime(dt);

    if (err == RV8803Error::OK) {
        LOG_INFO("RV8803: Current time: %04d-%02d-%02d %02d:%02d:%02d.%02d", 2000 + dt.year, dt.month, dt.date, dt.hours,
                 dt.minutes, dt.seconds, dt.hundredths);
    } else {
        LOG_WARN("RV8803: Failed to read time - %s", RV8803::errorToString(err));
    }

    // Check voltage
    err = rv8803->checkVoltage();
    LOG_INFO("RV8803: Voltage status: %s", (err == RV8803Error::OK) ? "OK" : "LOW");

    // Configuration
    LOG_INFO("RV8803: Update threshold: %lu seconds", rv8803->getUpdateThreshold());

    // Calibration offset
    int8_t offset = 0;
    if (rv8803->getCalibrationOffset(offset) == RV8803Error::OK) {
        LOG_INFO("RV8803: Calibration offset: %d", offset);
    }
}
