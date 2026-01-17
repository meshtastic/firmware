/**
 * @file RV8803.cpp
 * @brief Implementation of RV-8803-C7 Real-Time Clock Driver
 *
 * Following NASA's Power of 10 Rules for Safety-Critical Code.
 * Each function includes assertions and bounded operations.
 */

#include "RV8803.h"
#include "configuration.h"

// =============================================================================
// Construction and Initialization
// =============================================================================

RV8803::RV8803(uint32_t updateThresholdSecs)
    : _wire(nullptr), _i2cAddr(RV8803_I2C_ADDR), _initialized(false), _updateThresholdSecs(updateThresholdSecs)
{
    // NASA Rule #5: Assertions for invariants
    RV8803_ASSERT(updateThresholdSecs <= 86400); // Max 24 hours threshold
}

RV8803Error RV8803::begin(TwoWire &wirePort, uint8_t i2cAddr)
{
    // NASA Rule #5: Input validation assertions
    RV8803_ASSERT(i2cAddr != 0x00);
    RV8803_ASSERT(i2cAddr <= 0x7F);

    _wire = &wirePort;
    _i2cAddr = i2cAddr;
    _initialized = false;

    // Verify device is responding
    _wire->beginTransmission(_i2cAddr);
    uint8_t i2cResult = _wire->endTransmission();

    if (i2cResult != 0) {
        LOG_WARN("RV8803: Device not found at address 0x%02X (I2C error %d)", _i2cAddr, i2cResult);
        return RV8803Error::DEVICE_NOT_FOUND;
    }

    // Check voltage flags to ensure valid data
    RV8803Error voltageCheck = checkVoltage();
    if (voltageCheck == RV8803Error::VOLTAGE_LOW) {
        LOG_WARN("RV8803: Low voltage detected, RTC data may be invalid");
        // Continue initialization but caller should be aware
    }

    _initialized = true;
    LOG_INFO("RV8803: Initialized at address 0x%02X", _i2cAddr);

    return RV8803Error::OK;
}

bool RV8803::isInitialized() const
{
    return _initialized;
}

RV8803Error RV8803::checkVoltage()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);

    uint8_t flags = 0;
    RV8803Error err = readRegister(RV8803Reg::FLAG, flags);

    if (err != RV8803Error::OK) {
        return err;
    }

    // Check V1F (voltage dropped below threshold) or V2F (data loss possible)
    if ((flags & RV8803Bits::FLAG_V1F) || (flags & RV8803Bits::FLAG_V2F)) {
        return RV8803Error::VOLTAGE_LOW;
    }

    return RV8803Error::OK;
}

// =============================================================================
// Time and Date Operations
// =============================================================================

RV8803Error RV8803::getDateTime(RV8803DateTime &dt)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    // Read all time registers in one burst (0x10-0x17 = 8 bytes)
    uint8_t buffer[8];
    RV8803Error err = readRegisters(RV8803Reg::HUNDREDTHS, buffer, 8);

    if (err != RV8803Error::OK) {
        return err;
    }

    // Convert BCD to binary
    dt.hundredths = bcdToBin(buffer[0]);
    dt.seconds = bcdToBin(buffer[1] & 0x7F);
    dt.minutes = bcdToBin(buffer[2] & 0x7F);
    dt.hours = bcdToBin(buffer[3] & 0x3F);
    dt.weekday = buffer[4] & 0x7F;
    dt.date = bcdToBin(buffer[5] & 0x3F);
    dt.month = bcdToBin(buffer[6] & 0x1F);
    dt.year = bcdToBin(buffer[7]);

    // NASA Rule #5: Postcondition assertion
    RV8803_ASSERT(dt.seconds <= 59);
    RV8803_ASSERT(dt.minutes <= 59);

    return RV8803Error::OK;
}

RV8803Error RV8803::setDateTime(const RV8803DateTime &dt)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    // Validate input
    if (!validateDateTime(dt)) {
        LOG_WARN("RV8803: Invalid datetime values");
        return RV8803Error::INVALID_PARAM;
    }

    // Prepare BCD data buffer
    uint8_t buffer[8];
    buffer[0] = binToBcd(dt.hundredths);
    buffer[1] = binToBcd(dt.seconds);
    buffer[2] = binToBcd(dt.minutes);
    buffer[3] = binToBcd(dt.hours);
    buffer[4] = dt.weekday;
    buffer[5] = binToBcd(dt.date);
    buffer[6] = binToBcd(dt.month);
    buffer[7] = binToBcd(dt.year);

    // Write all registers in one burst
    RV8803Error err = writeRegisters(RV8803Reg::HUNDREDTHS, buffer, 8);

    if (err != RV8803Error::OK) {
        return err;
    }

    // Clear voltage low flags after successful time set
    uint8_t flags = 0;
    err = readRegister(RV8803Reg::FLAG, flags);
    if (err == RV8803Error::OK) {
        flags &= ~(RV8803Bits::FLAG_V1F | RV8803Bits::FLAG_V2F);
        err = writeRegister(RV8803Reg::FLAG, flags);
    }

    LOG_DEBUG("RV8803: DateTime set to %04d-%02d-%02d %02d:%02d:%02d", 2000 + dt.year, dt.month, dt.date, dt.hours, dt.minutes,
              dt.seconds);

    return err;
}

RV8803Error RV8803::getEpoch(uint32_t &epoch)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    RV8803DateTime dt;
    RV8803Error err = getDateTime(dt);

    if (err != RV8803Error::OK) {
        return err;
    }

    epoch = dateTimeToEpoch(dt);

    // NASA Rule #5: Postcondition assertion (reasonable epoch range 2000-2099)
    RV8803_ASSERT(epoch >= 946684800UL);  // 2000-01-01
    RV8803_ASSERT(epoch <= 4102444800UL); // 2100-01-01

    return RV8803Error::OK;
}

RV8803Error RV8803::setEpoch(uint32_t epoch)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(epoch >= 946684800UL); // Must be >= year 2000

    RV8803DateTime dt;
    epochToDateTime(epoch, dt);

    return setDateTime(dt);
}

RV8803Error RV8803::updateIfDelta(uint32_t newEpoch, uint32_t &deltaSeconds)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(newEpoch >= 946684800UL);

    uint32_t currentEpoch = 0;
    RV8803Error err = getEpoch(currentEpoch);

    if (err != RV8803Error::OK) {
        return err;
    }

    // Calculate absolute difference
    if (newEpoch >= currentEpoch) {
        deltaSeconds = newEpoch - currentEpoch;
    } else {
        deltaSeconds = currentEpoch - newEpoch;
    }

    // Check if delta meets threshold
    if (deltaSeconds < _updateThresholdSecs) {
        LOG_DEBUG("RV8803: Skip update, delta %lu sec < threshold %lu sec", deltaSeconds, _updateThresholdSecs);
        return RV8803Error::THRESHOLD_NOT_MET;
    }

    // Delta meets threshold, perform update
    LOG_INFO("RV8803: Updating time, delta %lu sec >= threshold %lu sec", deltaSeconds, _updateThresholdSecs);

    return setEpoch(newEpoch);
}

RV8803Error RV8803::resetHundredths()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    return writeRegister(RV8803Reg::HUNDREDTHS, 0x00);
}

// =============================================================================
// Alarm Operations
// =============================================================================

RV8803Error RV8803::setAlarm(const RV8803Alarm &alarm)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(alarm.minutes <= 59);
    RV8803_ASSERT(alarm.hours <= 23);

    // Validate parameters
    if (alarm.minutes > 59 || alarm.hours > 23) {
        return RV8803Error::INVALID_PARAM;
    }

    if (!alarm.useDate && (alarm.weekdayOrDate == 0 || alarm.weekdayOrDate > 0x7F)) {
        return RV8803Error::INVALID_PARAM;
    }

    if (alarm.useDate && (alarm.weekdayOrDate < 1 || alarm.weekdayOrDate > 31)) {
        return RV8803Error::INVALID_PARAM;
    }

    // Set WADA bit in Extension register
    uint8_t extension = 0;
    RV8803Error err = readRegister(RV8803Reg::EXTENSION, extension);
    if (err != RV8803Error::OK) {
        return err;
    }

    if (alarm.useDate) {
        extension |= RV8803Bits::EXT_WADA;
    } else {
        extension &= ~RV8803Bits::EXT_WADA;
    }

    err = writeRegister(RV8803Reg::EXTENSION, extension);
    if (err != RV8803Error::OK) {
        return err;
    }

    // Prepare alarm registers with enable bits
    uint8_t minAlarm = binToBcd(alarm.minutes);
    uint8_t hourAlarm = binToBcd(alarm.hours);
    uint8_t wdayDateAlarm = alarm.useDate ? binToBcd(alarm.weekdayOrDate) : alarm.weekdayOrDate;

    // Set or clear enable bits based on matchFlags
    if (!(alarm.matchFlags & static_cast<uint8_t>(RV8803AlarmMatch::MINUTES_MATCH))) {
        minAlarm |= RV8803Bits::ALARM_ENABLE;
    }
    if (!(alarm.matchFlags & static_cast<uint8_t>(RV8803AlarmMatch::HOURS_MATCH))) {
        hourAlarm |= RV8803Bits::ALARM_ENABLE;
    }
    if (!(alarm.matchFlags & static_cast<uint8_t>(RV8803AlarmMatch::WEEKDAY_MATCH))) {
        wdayDateAlarm |= RV8803Bits::ALARM_ENABLE;
    }

    // Write alarm registers
    uint8_t buffer[3] = {minAlarm, hourAlarm, wdayDateAlarm};
    return writeRegisters(RV8803Reg::MINUTES_ALARM, buffer, 3);
}

RV8803Error RV8803::getAlarm(RV8803Alarm &alarm)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    // Read Extension register for WADA bit
    uint8_t extension = 0;
    RV8803Error err = readRegister(RV8803Reg::EXTENSION, extension);
    if (err != RV8803Error::OK) {
        return err;
    }

    alarm.useDate = (extension & RV8803Bits::EXT_WADA) != 0;

    // Read alarm registers
    uint8_t buffer[3];
    err = readRegisters(RV8803Reg::MINUTES_ALARM, buffer, 3);
    if (err != RV8803Error::OK) {
        return err;
    }

    // Extract values and match flags
    alarm.matchFlags = 0;

    if (!(buffer[0] & RV8803Bits::ALARM_ENABLE)) {
        alarm.matchFlags |= static_cast<uint8_t>(RV8803AlarmMatch::MINUTES_MATCH);
    }
    alarm.minutes = bcdToBin(buffer[0] & 0x7F);

    if (!(buffer[1] & RV8803Bits::ALARM_ENABLE)) {
        alarm.matchFlags |= static_cast<uint8_t>(RV8803AlarmMatch::HOURS_MATCH);
    }
    alarm.hours = bcdToBin(buffer[1] & 0x3F);

    if (!(buffer[2] & RV8803Bits::ALARM_ENABLE)) {
        alarm.matchFlags |= static_cast<uint8_t>(RV8803AlarmMatch::WEEKDAY_MATCH);
    }

    if (alarm.useDate) {
        alarm.weekdayOrDate = bcdToBin(buffer[2] & 0x3F);
    } else {
        alarm.weekdayOrDate = buffer[2] & 0x7F;
    }

    return RV8803Error::OK;
}

RV8803Error RV8803::enableAlarmInterrupt(bool enable)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::CONTROL, RV8803Bits::CTRL_AIE, enable ? RV8803Bits::CTRL_AIE : 0);
}

RV8803Error RV8803::isAlarmTriggered(bool &triggered)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    uint8_t flags = 0;
    RV8803Error err = readRegister(RV8803Reg::FLAG, flags);

    if (err == RV8803Error::OK) {
        triggered = (flags & RV8803Bits::FLAG_AF) != 0;
    }

    return err;
}

RV8803Error RV8803::clearAlarmFlag()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::FLAG, RV8803Bits::FLAG_AF, 0);
}

// =============================================================================
// Timer Operations
// =============================================================================

RV8803Error RV8803::setTimer(const RV8803Timer &timer)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(timer.counterValue <= 4095);

    if (timer.counterValue > 4095) {
        return RV8803Error::INVALID_PARAM;
    }

    // Disable timer before configuration
    RV8803Error err = enableTimer(false);
    if (err != RV8803Error::OK) {
        return err;
    }

    // Set timer clock frequency in Extension register
    err = modifyRegister(RV8803Reg::EXTENSION, RV8803Bits::EXT_TD_MASK, static_cast<uint8_t>(timer.clockFrequency));
    if (err != RV8803Error::OK) {
        return err;
    }

    // Write timer counter value (12-bit split across two registers)
    uint8_t timerBuffer[2];
    timerBuffer[0] = timer.counterValue & 0xFF;
    timerBuffer[1] = (timer.counterValue >> 8) & 0x0F;

    return writeRegisters(RV8803Reg::TIMER_COUNTER_0, timerBuffer, 2);
}

RV8803Error RV8803::getTimer(RV8803Timer &timer)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    // Read Extension register for clock frequency
    uint8_t extension = 0;
    RV8803Error err = readRegister(RV8803Reg::EXTENSION, extension);
    if (err != RV8803Error::OK) {
        return err;
    }

    timer.clockFrequency = static_cast<RV8803TimerClock>(extension & RV8803Bits::EXT_TD_MASK);
    timer.repeatMode = (extension & RV8803Bits::EXT_TE) != 0;

    // Read timer counter
    uint8_t timerBuffer[2];
    err = readRegisters(RV8803Reg::TIMER_COUNTER_0, timerBuffer, 2);
    if (err != RV8803Error::OK) {
        return err;
    }

    timer.counterValue = timerBuffer[0] | ((timerBuffer[1] & 0x0F) << 8);

    // NASA Rule #5: Postcondition assertion
    RV8803_ASSERT(timer.counterValue <= 4095);

    return RV8803Error::OK;
}

RV8803Error RV8803::enableTimer(bool enable)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::EXTENSION, RV8803Bits::EXT_TE, enable ? RV8803Bits::EXT_TE : 0);
}

RV8803Error RV8803::enableTimerInterrupt(bool enable)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::CONTROL, RV8803Bits::CTRL_TIE, enable ? RV8803Bits::CTRL_TIE : 0);
}

RV8803Error RV8803::isTimerExpired(bool &expired)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    uint8_t flags = 0;
    RV8803Error err = readRegister(RV8803Reg::FLAG, flags);

    if (err == RV8803Error::OK) {
        expired = (flags & RV8803Bits::FLAG_TF) != 0;
    }

    return err;
}

RV8803Error RV8803::clearTimerFlag()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::FLAG, RV8803Bits::FLAG_TF, 0);
}

// =============================================================================
// Event/Timestamp Operations
// =============================================================================

RV8803Error RV8803::configureEventInput(bool captureEnable, bool risingEdge, RV8803EventDebounce debounce)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    uint8_t eventCtrl = 0;

    if (captureEnable) {
        eventCtrl |= RV8803Bits::EVT_ECP;
    }

    if (!risingEdge) {
        eventCtrl |= RV8803Bits::EVT_EHL;
    }

    eventCtrl |= static_cast<uint8_t>(debounce);

    return writeRegister(RV8803Reg::EVENT_CONTROL, eventCtrl);
}

RV8803Error RV8803::getTimestamp(RV8803Timestamp &ts)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    // Read timestamp registers (7 bytes: 0x20-0x26)
    uint8_t buffer[7];
    RV8803Error err = readRegisters(RV8803Reg::HUNDREDTHS_CAPTURE, buffer, 7);

    if (err != RV8803Error::OK) {
        return err;
    }

    ts.hundredths = bcdToBin(buffer[0]);
    ts.seconds = bcdToBin(buffer[1] & 0x7F);
    ts.minutes = bcdToBin(buffer[2] & 0x7F);
    ts.hours = bcdToBin(buffer[3] & 0x3F);
    ts.date = bcdToBin(buffer[4] & 0x3F);
    ts.month = bcdToBin(buffer[5] & 0x1F);
    ts.year = bcdToBin(buffer[6]);

    return RV8803Error::OK;
}

RV8803Error RV8803::isEventOccurred(bool &occurred)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    uint8_t flags = 0;
    RV8803Error err = readRegister(RV8803Reg::FLAG, flags);

    if (err == RV8803Error::OK) {
        occurred = (flags & RV8803Bits::FLAG_EVF) != 0;
    }

    return err;
}

RV8803Error RV8803::clearEventFlag()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    // Clear event flag and reset timestamp capture
    RV8803Error err = modifyRegister(RV8803Reg::FLAG, RV8803Bits::FLAG_EVF, 0);
    if (err != RV8803Error::OK) {
        return err;
    }

    // Reset event capture by setting ERST bit
    return modifyRegister(RV8803Reg::EVENT_CONTROL, RV8803Bits::EVT_ERST, RV8803Bits::EVT_ERST);
}

// =============================================================================
// Clock Output Operations
// =============================================================================

RV8803Error RV8803::setClockOutput(RV8803ClockOut freq)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return modifyRegister(RV8803Reg::EXTENSION, RV8803Bits::EXT_FD_MASK, static_cast<uint8_t>(freq));
}

RV8803Error RV8803::enableClockOutput(bool enable)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    // CLKOUT is enabled by default when FD bits are set
    // To disable, we can use the RAM register bit or set FD to a disabled state
    // For simplicity, we'll just set/clear the frequency
    if (enable) {
        return setClockOutput(RV8803ClockOut::FREQ_32768_HZ);
    } else {
        // Setting FD to 11 disables output
        return modifyRegister(RV8803Reg::EXTENSION, RV8803Bits::EXT_FD_MASK, 0x0C);
    }
}

// =============================================================================
// Calibration Operations
// =============================================================================

RV8803Error RV8803::setCalibrationOffset(int8_t offset)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(offset >= RV8803Config::MIN_OFFSET);
    RV8803_ASSERT(offset <= RV8803Config::MAX_OFFSET);

    if (offset < RV8803Config::MIN_OFFSET || offset > RV8803Config::MAX_OFFSET) {
        return RV8803Error::INVALID_PARAM;
    }

    // Offset register uses 2's complement for negative values
    uint8_t regValue = static_cast<uint8_t>(offset) & 0x7F;

    return writeRegister(RV8803Reg::OFFSET, regValue);
}

RV8803Error RV8803::getCalibrationOffset(int8_t &offset)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    uint8_t regValue = 0;
    RV8803Error err = readRegister(RV8803Reg::OFFSET, regValue);

    if (err == RV8803Error::OK) {
        // Convert from 7-bit signed to int8_t
        if (regValue & 0x40) {
            // Negative value, sign extend
            offset = static_cast<int8_t>(regValue | 0x80);
        } else {
            offset = static_cast<int8_t>(regValue & 0x3F);
        }
    }

    return err;
}

// =============================================================================
// Status and Control
// =============================================================================

RV8803Error RV8803::getFlags(uint8_t &flags)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return readRegister(RV8803Reg::FLAG, flags);
}

RV8803Error RV8803::clearAllFlags()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    // Clear all clearable flags (UF, TF, AF, EVF) but preserve V1F and V2F for reading
    uint8_t clearMask = RV8803Bits::FLAG_UF | RV8803Bits::FLAG_TF | RV8803Bits::FLAG_AF | RV8803Bits::FLAG_EVF;

    return modifyRegister(RV8803Reg::FLAG, clearMask, 0);
}

RV8803Error RV8803::softwareReset()
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);
    RV8803_ASSERT(_wire != nullptr);

    LOG_WARN("RV8803: Performing software reset");

    RV8803Error err = modifyRegister(RV8803Reg::CONTROL, RV8803Bits::CTRL_RESET, RV8803Bits::CTRL_RESET);

    // Reset bit is auto-cleared, wait briefly for reset to complete
    delay(1);

    return err;
}

RV8803Error RV8803::readRAM(uint8_t &value)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return readRegister(RV8803Reg::RAM, value);
}

RV8803Error RV8803::writeRAM(uint8_t value)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_initialized);

    return writeRegister(RV8803Reg::RAM, value);
}

// =============================================================================
// Configuration
// =============================================================================

void RV8803::setUpdateThreshold(uint32_t thresholdSecs)
{
    // NASA Rule #5: Bound check assertion
    RV8803_ASSERT(thresholdSecs <= 86400);

    _updateThresholdSecs = thresholdSecs;
}

uint32_t RV8803::getUpdateThreshold() const
{
    return _updateThresholdSecs;
}

// =============================================================================
// Private Low-Level I2C Operations
// =============================================================================

RV8803Error RV8803::readRegister(uint8_t reg, uint8_t &value)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);
    RV8803_ASSERT(reg <= 0x2F);

    // NASA Rule #2: Fixed loop bound for retries
    for (uint8_t retry = 0; retry < RV8803Config::MAX_I2C_RETRIES; retry++) {
        _wire->beginTransmission(_i2cAddr);
        _wire->write(reg);

        if (_wire->endTransmission(false) != 0) {
            continue; // Retry on transmission error
        }

        if (_wire->requestFrom(_i2cAddr, (uint8_t)1) != 1) {
            continue; // Retry if we didn't get the expected byte
        }

        value = _wire->read();
        return RV8803Error::OK;
    }

    LOG_WARN("RV8803: I2C read failed for register 0x%02X after %d retries", reg, RV8803Config::MAX_I2C_RETRIES);
    return RV8803Error::I2C_ERROR;
}

RV8803Error RV8803::writeRegister(uint8_t reg, uint8_t value)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);
    RV8803_ASSERT(reg <= 0x2F);

    // NASA Rule #2: Fixed loop bound for retries
    for (uint8_t retry = 0; retry < RV8803Config::MAX_I2C_RETRIES; retry++) {
        _wire->beginTransmission(_i2cAddr);
        _wire->write(reg);
        _wire->write(value);

        if (_wire->endTransmission() == 0) {
            return RV8803Error::OK;
        }
    }

    LOG_WARN("RV8803: I2C write failed for register 0x%02X after %d retries", reg, RV8803Config::MAX_I2C_RETRIES);
    return RV8803Error::I2C_ERROR;
}

RV8803Error RV8803::readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t count)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);
    RV8803_ASSERT(buffer != nullptr);
    RV8803_ASSERT(count > 0);
    RV8803_ASSERT(count <= RV8803Config::MAX_BURST_READ);

    // NASA Rule #9: Validate pointer
    if (buffer == nullptr || count == 0 || count > RV8803Config::MAX_BURST_READ) {
        return RV8803Error::INVALID_PARAM;
    }

    // NASA Rule #2: Fixed loop bound for retries
    for (uint8_t retry = 0; retry < RV8803Config::MAX_I2C_RETRIES; retry++) {
        _wire->beginTransmission(_i2cAddr);
        _wire->write(startReg);

        if (_wire->endTransmission(false) != 0) {
            continue;
        }

        uint8_t received = _wire->requestFrom(_i2cAddr, count);
        if (received != count) {
            continue;
        }

        // NASA Rule #2: Loop bounded by count parameter (max 16)
        for (uint8_t i = 0; i < count; i++) {
            buffer[i] = _wire->read();
        }

        return RV8803Error::OK;
    }

    LOG_WARN("RV8803: I2C burst read failed starting at 0x%02X after %d retries", startReg, RV8803Config::MAX_I2C_RETRIES);
    return RV8803Error::I2C_ERROR;
}

RV8803Error RV8803::writeRegisters(uint8_t startReg, const uint8_t *buffer, uint8_t count)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);
    RV8803_ASSERT(buffer != nullptr);
    RV8803_ASSERT(count > 0);
    RV8803_ASSERT(count <= RV8803Config::MAX_BURST_READ);

    // NASA Rule #9: Validate pointer
    if (buffer == nullptr || count == 0 || count > RV8803Config::MAX_BURST_READ) {
        return RV8803Error::INVALID_PARAM;
    }

    // NASA Rule #2: Fixed loop bound for retries
    for (uint8_t retry = 0; retry < RV8803Config::MAX_I2C_RETRIES; retry++) {
        _wire->beginTransmission(_i2cAddr);
        _wire->write(startReg);

        // NASA Rule #2: Loop bounded by count parameter (max 16)
        for (uint8_t i = 0; i < count; i++) {
            _wire->write(buffer[i]);
        }

        if (_wire->endTransmission() == 0) {
            return RV8803Error::OK;
        }
    }

    LOG_WARN("RV8803: I2C burst write failed starting at 0x%02X after %d retries", startReg, RV8803Config::MAX_I2C_RETRIES);
    return RV8803Error::I2C_ERROR;
}

RV8803Error RV8803::modifyRegister(uint8_t reg, uint8_t mask, uint8_t value)
{
    // NASA Rule #5: Precondition assertions
    RV8803_ASSERT(_wire != nullptr);
    RV8803_ASSERT(reg <= 0x2F);

    uint8_t currentValue = 0;
    RV8803Error err = readRegister(reg, currentValue);

    if (err != RV8803Error::OK) {
        return err;
    }

    uint8_t newValue = (currentValue & ~mask) | (value & mask);

    // Only write if value actually changed
    if (newValue != currentValue) {
        return writeRegister(reg, newValue);
    }

    return RV8803Error::OK;
}

// =============================================================================
// Private Utility Functions
// =============================================================================

uint8_t RV8803::bcdToBin(uint8_t bcd)
{
    // NASA Rule #5: Range assertion
    RV8803_ASSERT((bcd & 0x0F) <= 9);
    RV8803_ASSERT(((bcd >> 4) & 0x0F) <= 9);

    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t RV8803::binToBcd(uint8_t bin)
{
    // NASA Rule #5: Range assertion
    RV8803_ASSERT(bin <= 99);

    return ((bin / 10) << 4) | (bin % 10);
}

bool RV8803::validateDateTime(const RV8803DateTime &dt)
{
    // NASA Rule #5: Comprehensive validation
    if (dt.hundredths > 99)
        return false;
    if (dt.seconds > 59)
        return false;
    if (dt.minutes > 59)
        return false;
    if (dt.hours > 23)
        return false;
    if (dt.month < 1 || dt.month > 12)
        return false;
    if (dt.year > 99)
        return false;
    if (dt.date < 1 || dt.date > daysInMonth(dt.month, dt.year))
        return false;
    if (dt.weekday == 0 || dt.weekday > 0x7F)
        return false;

    return true;
}

bool RV8803::isLeapYear(uint8_t year)
{
    // NASA Rule #5: Input assertion
    RV8803_ASSERT(year <= 99);

    // Year is 0-99 representing 2000-2099
    // 2000 is a leap year, then every 4 years
    uint16_t fullYear = 2000 + year;
    return (fullYear % 4 == 0);
}

uint8_t RV8803::daysInMonth(uint8_t month, uint8_t year)
{
    // NASA Rule #5: Input assertions
    RV8803_ASSERT(month >= 1 && month <= 12);
    RV8803_ASSERT(year <= 99);

    // Days per month lookup (index 0 unused, 1=Jan, 12=Dec)
    static const uint8_t daysPerMonth[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12) {
        return 0;
    }

    uint8_t days = daysPerMonth[month];

    // February in leap year
    if (month == 2 && isLeapYear(year)) {
        days = 29;
    }

    return days;
}

uint8_t RV8803::calculateWeekday(uint8_t year, uint8_t month, uint8_t date)
{
    // NASA Rule #5: Input assertions
    RV8803_ASSERT(year <= 99);
    RV8803_ASSERT(month >= 1 && month <= 12);
    RV8803_ASSERT(date >= 1 && date <= 31);

    // Zeller's congruence adapted for 2000-2099
    // Returns bit-mapped weekday (1=Sunday, 2=Monday, etc.)

    int y = 2000 + year;
    int m = month;
    int d = date;

    // Adjust for January and February
    if (m < 3) {
        m += 12;
        y -= 1;
    }

    int k = y % 100;
    int j = y / 100;

    // Zeller's formula
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    // Convert h to bit-mapped weekday (0=Saturday in Zeller, we need Sunday=1)
    // Zeller: 0=Sat, 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri
    // We need: 1=Sun, 2=Mon, 4=Tue, 8=Wed, 16=Thu, 32=Fri, 64=Sat
    static const uint8_t zellerToWeekday[7] = {0x40, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20};

    h = ((h % 7) + 7) % 7; // Ensure positive
    return zellerToWeekday[h];
}

// =============================================================================
// Static Utility Functions
// =============================================================================

uint32_t RV8803::dateTimeToEpoch(const RV8803DateTime &dt)
{
    // NASA Rule #5: Input validation assertion
    RV8803_ASSERT(dt.year <= 99);
    RV8803_ASSERT(dt.month >= 1 && dt.month <= 12);

    // Calculate days since 1970-01-01
    uint32_t days = 0;

    // Days from 1970 to 1999 (30 years)
    // 1972, 1976, 1980, 1984, 1988, 1992, 1996 = 7 leap years
    static const uint32_t daysTo2000 = 10957; // Pre-calculated

    days = daysTo2000;

    // Add days for complete years from 2000 to (2000+year-1)
    // NASA Rule #2: Loop bounded by year (max 99)
    for (uint8_t y = 0; y < dt.year; y++) {
        days += isLeapYear(y) ? 366 : 365;
    }

    // Add days for complete months in current year
    // NASA Rule #2: Loop bounded by month (max 12)
    for (uint8_t m = 1; m < dt.month; m++) {
        days += daysInMonth(m, dt.year);
    }

    // Add days in current month
    days += dt.date - 1;

    // Convert to seconds and add time
    uint32_t epoch = days * 86400UL;
    epoch += dt.hours * 3600UL;
    epoch += dt.minutes * 60UL;
    epoch += dt.seconds;

    return epoch;
}

void RV8803::epochToDateTime(uint32_t epoch, RV8803DateTime &dt)
{
    // NASA Rule #5: Input assertion (must be >= year 2000)
    RV8803_ASSERT(epoch >= 946684800UL);

    // Start from 2000-01-01 00:00:00 (epoch 946684800)
    static const uint32_t epoch2000 = 946684800UL;

    uint32_t remaining = epoch - epoch2000;

    // Extract time components
    dt.seconds = remaining % 60;
    remaining /= 60;
    dt.minutes = remaining % 60;
    remaining /= 60;
    dt.hours = remaining % 24;
    remaining /= 24;

    // remaining is now days since 2000-01-01
    uint32_t daysSince2000 = remaining;

    // Find year
    // NASA Rule #2: Loop bounded by MAX_YEAR (99)
    dt.year = 0;
    for (uint8_t y = 0; y <= RV8803Config::MAX_YEAR; y++) {
        uint16_t daysInYear = isLeapYear(y) ? 366 : 365;
        if (daysSince2000 < daysInYear) {
            dt.year = y;
            break;
        }
        daysSince2000 -= daysInYear;
        dt.year = y + 1;
    }

    // Find month
    // NASA Rule #2: Loop bounded (max 12 iterations)
    dt.month = 1;
    for (uint8_t m = 1; m <= 12; m++) {
        uint8_t dim = daysInMonth(m, dt.year);
        if (daysSince2000 < dim) {
            dt.month = m;
            break;
        }
        daysSince2000 -= dim;
        dt.month = m + 1;
    }

    dt.date = daysSince2000 + 1;
    dt.hundredths = 0;

    // Calculate weekday
    dt.weekday = calculateWeekday(dt.year, dt.month, dt.date);

    // NASA Rule #5: Postcondition assertions
    RV8803_ASSERT(dt.month >= 1 && dt.month <= 12);
    RV8803_ASSERT(dt.date >= 1 && dt.date <= 31);
}

const char *RV8803::errorToString(RV8803Error err)
{
    switch (err) {
    case RV8803Error::OK:
        return "OK";
    case RV8803Error::I2C_ERROR:
        return "I2C_ERROR";
    case RV8803Error::INVALID_PARAM:
        return "INVALID_PARAM";
    case RV8803Error::NOT_INITIALIZED:
        return "NOT_INITIALIZED";
    case RV8803Error::TIME_INVALID:
        return "TIME_INVALID";
    case RV8803Error::VOLTAGE_LOW:
        return "VOLTAGE_LOW";
    case RV8803Error::WRITE_VERIFY_FAILED:
        return "WRITE_VERIFY_FAILED";
    case RV8803Error::THRESHOLD_NOT_MET:
        return "THRESHOLD_NOT_MET";
    case RV8803Error::DEVICE_NOT_FOUND:
        return "DEVICE_NOT_FOUND";
    default:
        return "UNKNOWN";
    }
}
