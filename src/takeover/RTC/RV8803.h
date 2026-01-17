/**
 * @file RV8803.h
 * @brief Driver for the RV-8803-C7 Real-Time Clock Module
 *
 * This driver follows NASA's Power of 10 Rules for Safety-Critical Code:
 * 1. Simple control flow - no goto, setjmp/longjmp, or recursion
 * 2. Fixed loop bounds - all loops have provable upper bounds
 * 3. No dynamic memory allocation after initialization
 * 4. Functions limited to ~60 lines (one printed page)
 * 5. Minimum 2 assertions per function
 * 6. Data objects at smallest possible scope
 * 7. All return values checked
 * 8. Limited preprocessor use
 * 9. Restricted pointer use
 * 10. Compile with all warnings enabled, use static analysis
 *
 * @see https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-8803-C7.pdf
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

// =============================================================================
// NASA Rule #5 Support: Assertion Macros
// =============================================================================

#ifndef RV8803_ASSERT
#ifdef NDEBUG
#define RV8803_ASSERT(condition) ((void)0)
#else
#define RV8803_ASSERT(condition)                                                                                                   \
    do {                                                                                                                           \
        if (!(condition)) {                                                                                                        \
            LOG_ERROR("RV8803 ASSERT failed: %s at %s:%d", #condition, __FILE__, __LINE__);                                        \
        }                                                                                                                          \
    } while (0)
#endif
#endif

// =============================================================================
// I2C Address
// =============================================================================

static constexpr uint8_t RV8803_I2C_ADDR = 0x32; // 7-bit I2C address

// =============================================================================
// Register Addresses (NASA Rule #8: minimal, necessary preprocessor use)
// =============================================================================

namespace RV8803Reg {

// Time and Calendar Registers (BCD format)
static constexpr uint8_t HUNDREDTHS = 0x10;
static constexpr uint8_t SECONDS = 0x11;
static constexpr uint8_t MINUTES = 0x12;
static constexpr uint8_t HOURS = 0x13;
static constexpr uint8_t WEEKDAY = 0x14;
static constexpr uint8_t DATE = 0x15;
static constexpr uint8_t MONTH = 0x16;
static constexpr uint8_t YEAR = 0x17;

// RAM Register
static constexpr uint8_t RAM = 0x07;

// Alarm Registers
static constexpr uint8_t MINUTES_ALARM = 0x18;
static constexpr uint8_t HOURS_ALARM = 0x19;
static constexpr uint8_t WEEKDAY_DATE_ALARM = 0x1A;

// Timer Registers
static constexpr uint8_t TIMER_COUNTER_0 = 0x1B;
static constexpr uint8_t TIMER_COUNTER_1 = 0x1C;

// Control Registers
static constexpr uint8_t EXTENSION = 0x1D;
static constexpr uint8_t FLAG = 0x1E;
static constexpr uint8_t CONTROL = 0x1F;

// Timestamp Registers
static constexpr uint8_t HUNDREDTHS_CAPTURE = 0x20;
static constexpr uint8_t SECONDS_CAPTURE = 0x21;
static constexpr uint8_t MINUTES_CAPTURE = 0x22;
static constexpr uint8_t HOURS_CAPTURE = 0x23;
static constexpr uint8_t DATE_CAPTURE = 0x24;
static constexpr uint8_t MONTH_CAPTURE = 0x25;
static constexpr uint8_t YEAR_CAPTURE = 0x26;

// Offset and Event Registers
static constexpr uint8_t OFFSET = 0x2C;
static constexpr uint8_t EVENT_CONTROL = 0x2F;

} // namespace RV8803Reg

// =============================================================================
// Register Bit Definitions
// =============================================================================

namespace RV8803Bits {

// Extension Register (0x1D) bits
static constexpr uint8_t EXT_TEST = (1 << 7);  // Test mode (must be 0)
static constexpr uint8_t EXT_WADA = (1 << 6);  // Week Alarm / Date Alarm select
static constexpr uint8_t EXT_USEL = (1 << 5);  // Update interrupt select
static constexpr uint8_t EXT_TE = (1 << 4);    // Timer enable
static constexpr uint8_t EXT_FD_MASK = 0x0C;   // Frequency output select (bits 3:2)
static constexpr uint8_t EXT_TD_MASK = 0x03;   // Timer clock select (bits 1:0)

// Flag Register (0x1E) bits
static constexpr uint8_t FLAG_UF = (1 << 5);  // Update flag
static constexpr uint8_t FLAG_TF = (1 << 4);  // Timer flag
static constexpr uint8_t FLAG_AF = (1 << 3);  // Alarm flag
static constexpr uint8_t FLAG_EVF = (1 << 2); // Event flag
static constexpr uint8_t FLAG_V2F = (1 << 1); // Voltage low flag 2
static constexpr uint8_t FLAG_V1F = (1 << 0); // Voltage low flag 1

// Control Register (0x1F) bits
static constexpr uint8_t CTRL_UIE = (1 << 5);   // Update interrupt enable
static constexpr uint8_t CTRL_TIE = (1 << 4);   // Timer interrupt enable
static constexpr uint8_t CTRL_AIE = (1 << 3);   // Alarm interrupt enable
static constexpr uint8_t CTRL_EIE = (1 << 2);   // Event interrupt enable
static constexpr uint8_t CTRL_RESET = (1 << 0); // Software reset

// Alarm Enable bit (bit 7 of alarm registers)
static constexpr uint8_t ALARM_ENABLE = (1 << 7);

// Event Control Register (0x2F) bits
static constexpr uint8_t EVT_ECP = (1 << 7);     // Event capture enable
static constexpr uint8_t EVT_EHL = (1 << 6);     // Event high/low select
static constexpr uint8_t EVT_ET_MASK = 0x30;     // Event filter (bits 5:4)
static constexpr uint8_t EVT_ERST = (1 << 0);    // Event reset

} // namespace RV8803Bits

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @brief Error codes for RV8803 operations (NASA Rule #7: check all returns)
 */
enum class RV8803Error : uint8_t {
    OK = 0,                    // Operation successful
    I2C_ERROR = 1,             // I2C communication failed
    INVALID_PARAM = 2,         // Invalid parameter provided
    NOT_INITIALIZED = 3,       // Device not initialized
    TIME_INVALID = 4,          // Time value out of valid range
    VOLTAGE_LOW = 5,           // Backup voltage too low (data may be invalid)
    WRITE_VERIFY_FAILED = 6,   // Write verification failed
    THRESHOLD_NOT_MET = 7,     // Time difference below update threshold
    DEVICE_NOT_FOUND = 8       // Device not responding at I2C address
};

/**
 * @brief Weekday values (bit-mapped for alarm matching)
 */
enum class RV8803Weekday : uint8_t {
    SUNDAY = 0x01,
    MONDAY = 0x02,
    TUESDAY = 0x04,
    WEDNESDAY = 0x08,
    THURSDAY = 0x10,
    FRIDAY = 0x20,
    SATURDAY = 0x40
};

/**
 * @brief Clock output frequencies
 */
enum class RV8803ClockOut : uint8_t {
    FREQ_32768_HZ = 0x00,
    FREQ_1024_HZ = 0x04,
    FREQ_1_HZ = 0x08
};

/**
 * @brief Timer clock frequencies
 */
enum class RV8803TimerClock : uint8_t {
    FREQ_4096_HZ = 0x00,   // 244.14 us per tick
    FREQ_64_HZ = 0x01,     // 15.625 ms per tick
    FREQ_1_HZ = 0x02,      // 1 second per tick
    FREQ_1_60_HZ = 0x03    // 60 seconds per tick
};

/**
 * @brief Event debounce time settings
 */
enum class RV8803EventDebounce : uint8_t {
    NONE = 0x00,
    DEBOUNCE_256HZ = 0x10,
    DEBOUNCE_64HZ = 0x20,
    DEBOUNCE_8HZ = 0x30
};

/**
 * @brief Alarm match criteria
 */
enum class RV8803AlarmMatch : uint8_t {
    MINUTES_MATCH = 0x01,
    HOURS_MATCH = 0x02,
    WEEKDAY_MATCH = 0x04,  // When WADA=0
    DATE_MATCH = 0x04      // When WADA=1
};

// =============================================================================
// Data Structures (NASA Rule #3: no dynamic allocation)
// =============================================================================

/**
 * @brief Date/Time structure - all fields stored in binary (not BCD)
 */
struct RV8803DateTime {
    uint8_t hundredths;  // 0-99
    uint8_t seconds;     // 0-59
    uint8_t minutes;     // 0-59
    uint8_t hours;       // 0-23 (24-hour format)
    uint8_t weekday;     // 1=Sunday, 2=Monday, ... 64=Saturday (bit-mapped)
    uint8_t date;        // 1-31
    uint8_t month;       // 1-12
    uint8_t year;        // 0-99 (represents 2000-2099)
};

/**
 * @brief Alarm configuration structure
 */
struct RV8803Alarm {
    uint8_t minutes;          // 0-59
    uint8_t hours;            // 0-23
    uint8_t weekdayOrDate;    // Weekday (bit-mapped) or Date (1-31)
    bool useDate;             // true=match date, false=match weekday
    uint8_t matchFlags;       // Which fields to match (RV8803AlarmMatch flags)
};

/**
 * @brief Timer configuration structure
 */
struct RV8803Timer {
    uint16_t counterValue;           // 0-4095 (12-bit)
    RV8803TimerClock clockFrequency;
    bool repeatMode;                 // true=auto-reload, false=single-shot
};

/**
 * @brief Timestamp capture structure
 */
struct RV8803Timestamp {
    uint8_t hundredths;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t date;
    uint8_t month;
    uint8_t year;
};

// =============================================================================
// Configuration Constants
// =============================================================================

namespace RV8803Config {

// Time update threshold in seconds (5 minutes default)
static constexpr uint32_t DEFAULT_UPDATE_THRESHOLD_SECS = 300;

// Maximum I2C retry count (NASA Rule #2: fixed loop bounds)
static constexpr uint8_t MAX_I2C_RETRIES = 3;

// Maximum number of registers to read in one burst
static constexpr uint8_t MAX_BURST_READ = 16;

// Valid year range (2000-2099)
static constexpr uint8_t MIN_YEAR = 0;
static constexpr uint8_t MAX_YEAR = 99;

// Calibration offset range
static constexpr int8_t MIN_OFFSET = -64;
static constexpr int8_t MAX_OFFSET = 63;

} // namespace RV8803Config

// =============================================================================
// RV8803 Class Definition
// =============================================================================

/**
 * @brief Driver class for RV-8803-C7 Real-Time Clock Module
 *
 * This class provides complete control of the RV-8803-C7 RTC following
 * NASA's Power of 10 rules for safety-critical code.
 *
 * Features:
 * - Time/date read/write with hundredths precision
 * - Configurable alarm with multiple match criteria
 * - Countdown timer with interrupt support
 * - External event timestamp capture
 * - Clock output (32.768kHz, 1.024kHz, 1Hz)
 * - Temperature-compensated calibration
 * - Wear-leveling with configurable update threshold
 */
class RV8803 {
  public:
    // =========================================================================
    // Construction and Initialization
    // =========================================================================

    /**
     * @brief Construct RV8803 driver instance
     * @param updateThresholdSecs Minimum time difference (seconds) to trigger RTC update
     */
    explicit RV8803(uint32_t updateThresholdSecs = RV8803Config::DEFAULT_UPDATE_THRESHOLD_SECS);

    /**
     * @brief Initialize the RV8803 device
     * @param wirePort Reference to TwoWire instance (Wire or Wire1)
     * @param i2cAddr I2C address (default 0x32)
     * @return RV8803Error::OK on success, error code otherwise
     */
    RV8803Error begin(TwoWire &wirePort, uint8_t i2cAddr = RV8803_I2C_ADDR);

    /**
     * @brief Check if device is initialized and responding
     * @return true if device is ready
     */
    bool isInitialized() const;

    /**
     * @brief Check if backup voltage is sufficient
     * @return RV8803Error::OK if voltage OK, RV8803Error::VOLTAGE_LOW otherwise
     */
    RV8803Error checkVoltage();

    // =========================================================================
    // Time and Date Operations
    // =========================================================================

    /**
     * @brief Read current date/time from RTC
     * @param[out] dt DateTime structure to populate
     * @return RV8803Error::OK on success
     */
    RV8803Error getDateTime(RV8803DateTime &dt);

    /**
     * @brief Set date/time on RTC
     * @param dt DateTime structure with values to set
     * @return RV8803Error::OK on success
     */
    RV8803Error setDateTime(const RV8803DateTime &dt);

    /**
     * @brief Get Unix epoch time (seconds since 1970-01-01 00:00:00 UTC)
     * @param[out] epoch Output epoch value
     * @return RV8803Error::OK on success
     */
    RV8803Error getEpoch(uint32_t &epoch);

    /**
     * @brief Set time from Unix epoch
     * @param epoch Seconds since 1970-01-01 00:00:00 UTC
     * @return RV8803Error::OK on success
     */
    RV8803Error setEpoch(uint32_t epoch);

    /**
     * @brief Conditionally update RTC if time difference exceeds threshold
     * @param newEpoch New epoch time to potentially set
     * @param[out] deltaSeconds Actual difference in seconds (absolute value)
     * @return RV8803Error::OK if updated, RV8803Error::THRESHOLD_NOT_MET if skipped
     *
     * This method implements wear-leveling by only updating the RTC when
     * the time difference exceeds the configured threshold (default 5 minutes).
     */
    RV8803Error updateIfDelta(uint32_t newEpoch, uint32_t &deltaSeconds);

    /**
     * @brief Reset hundredths counter to zero (for precise synchronization)
     * @return RV8803Error::OK on success
     */
    RV8803Error resetHundredths();

    // =========================================================================
    // Alarm Operations
    // =========================================================================

    /**
     * @brief Configure and enable alarm
     * @param alarm Alarm configuration structure
     * @return RV8803Error::OK on success
     */
    RV8803Error setAlarm(const RV8803Alarm &alarm);

    /**
     * @brief Read current alarm configuration
     * @param[out] alarm Alarm structure to populate
     * @return RV8803Error::OK on success
     */
    RV8803Error getAlarm(RV8803Alarm &alarm);

    /**
     * @brief Enable or disable alarm interrupt
     * @param enable true to enable, false to disable
     * @return RV8803Error::OK on success
     */
    RV8803Error enableAlarmInterrupt(bool enable);

    /**
     * @brief Check if alarm has triggered
     * @param[out] triggered true if alarm flag is set
     * @return RV8803Error::OK on success
     */
    RV8803Error isAlarmTriggered(bool &triggered);

    /**
     * @brief Clear alarm flag
     * @return RV8803Error::OK on success
     */
    RV8803Error clearAlarmFlag();

    // =========================================================================
    // Timer Operations
    // =========================================================================

    /**
     * @brief Configure countdown timer
     * @param timer Timer configuration structure
     * @return RV8803Error::OK on success
     */
    RV8803Error setTimer(const RV8803Timer &timer);

    /**
     * @brief Read current timer configuration
     * @param[out] timer Timer structure to populate
     * @return RV8803Error::OK on success
     */
    RV8803Error getTimer(RV8803Timer &timer);

    /**
     * @brief Enable or disable timer
     * @param enable true to enable, false to disable
     * @return RV8803Error::OK on success
     */
    RV8803Error enableTimer(bool enable);

    /**
     * @brief Enable or disable timer interrupt
     * @param enable true to enable, false to disable
     * @return RV8803Error::OK on success
     */
    RV8803Error enableTimerInterrupt(bool enable);

    /**
     * @brief Check if timer has expired
     * @param[out] expired true if timer flag is set
     * @return RV8803Error::OK on success
     */
    RV8803Error isTimerExpired(bool &expired);

    /**
     * @brief Clear timer flag
     * @return RV8803Error::OK on success
     */
    RV8803Error clearTimerFlag();

    // =========================================================================
    // Event/Timestamp Operations
    // =========================================================================

    /**
     * @brief Configure external event input
     * @param captureEnable Enable timestamp capture on event
     * @param risingEdge true=rising edge, false=falling edge
     * @param debounce Debounce filter setting
     * @return RV8803Error::OK on success
     */
    RV8803Error configureEventInput(bool captureEnable, bool risingEdge, RV8803EventDebounce debounce);

    /**
     * @brief Read captured timestamp
     * @param[out] ts Timestamp structure to populate
     * @return RV8803Error::OK on success
     */
    RV8803Error getTimestamp(RV8803Timestamp &ts);

    /**
     * @brief Check if event has occurred
     * @param[out] occurred true if event flag is set
     * @return RV8803Error::OK on success
     */
    RV8803Error isEventOccurred(bool &occurred);

    /**
     * @brief Clear event flag and reset timestamp capture
     * @return RV8803Error::OK on success
     */
    RV8803Error clearEventFlag();

    // =========================================================================
    // Clock Output Operations
    // =========================================================================

    /**
     * @brief Set clock output frequency
     * @param freq Desired output frequency
     * @return RV8803Error::OK on success
     */
    RV8803Error setClockOutput(RV8803ClockOut freq);

    /**
     * @brief Enable or disable clock output
     * @param enable true to enable CLKOUT pin
     * @return RV8803Error::OK on success
     */
    RV8803Error enableClockOutput(bool enable);

    // =========================================================================
    // Calibration Operations
    // =========================================================================

    /**
     * @brief Set calibration offset
     * @param offset Offset value (-64 to +63), ~0.2384 ppm per step
     * @return RV8803Error::OK on success
     */
    RV8803Error setCalibrationOffset(int8_t offset);

    /**
     * @brief Get current calibration offset
     * @param[out] offset Current offset value
     * @return RV8803Error::OK on success
     */
    RV8803Error getCalibrationOffset(int8_t &offset);

    // =========================================================================
    // Status and Control
    // =========================================================================

    /**
     * @brief Get all flag register values
     * @param[out] flags Raw flag register value
     * @return RV8803Error::OK on success
     */
    RV8803Error getFlags(uint8_t &flags);

    /**
     * @brief Clear all interrupt flags
     * @return RV8803Error::OK on success
     */
    RV8803Error clearAllFlags();

    /**
     * @brief Perform software reset
     * @return RV8803Error::OK on success
     */
    RV8803Error softwareReset();

    /**
     * @brief Read user RAM byte
     * @param[out] value RAM content
     * @return RV8803Error::OK on success
     */
    RV8803Error readRAM(uint8_t &value);

    /**
     * @brief Write user RAM byte
     * @param value Value to store
     * @return RV8803Error::OK on success
     */
    RV8803Error writeRAM(uint8_t value);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set the time update threshold
     * @param thresholdSecs Minimum difference in seconds to trigger update
     */
    void setUpdateThreshold(uint32_t thresholdSecs);

    /**
     * @brief Get the current update threshold
     * @return Current threshold in seconds
     */
    uint32_t getUpdateThreshold() const;

    // =========================================================================
    // Utility Functions
    // =========================================================================

    /**
     * @brief Convert DateTime to Unix epoch
     * @param dt DateTime structure
     * @return Unix epoch seconds
     */
    static uint32_t dateTimeToEpoch(const RV8803DateTime &dt);

    /**
     * @brief Convert Unix epoch to DateTime
     * @param epoch Unix epoch seconds
     * @param[out] dt DateTime structure to populate
     */
    static void epochToDateTime(uint32_t epoch, RV8803DateTime &dt);

    /**
     * @brief Convert error code to string (for logging)
     * @param err Error code
     * @return Human-readable error string
     */
    static const char *errorToString(RV8803Error err);

  private:
    // =========================================================================
    // Private Member Variables (NASA Rule #6: minimal scope)
    // =========================================================================

    TwoWire *_wire;                    // I2C interface pointer
    uint8_t _i2cAddr;                  // I2C address
    bool _initialized;                 // Initialization flag
    uint32_t _updateThresholdSecs;     // Time update threshold

    // =========================================================================
    // Private Low-Level I2C Operations
    // =========================================================================

    /**
     * @brief Read single register
     * @param reg Register address
     * @param[out] value Read value
     * @return RV8803Error::OK on success
     */
    RV8803Error readRegister(uint8_t reg, uint8_t &value);

    /**
     * @brief Write single register
     * @param reg Register address
     * @param value Value to write
     * @return RV8803Error::OK on success
     */
    RV8803Error writeRegister(uint8_t reg, uint8_t value);

    /**
     * @brief Read multiple consecutive registers
     * @param startReg Starting register address
     * @param[out] buffer Buffer to store read data
     * @param count Number of registers to read (max 16)
     * @return RV8803Error::OK on success
     */
    RV8803Error readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t count);

    /**
     * @brief Write multiple consecutive registers
     * @param startReg Starting register address
     * @param buffer Data to write
     * @param count Number of registers to write (max 16)
     * @return RV8803Error::OK on success
     */
    RV8803Error writeRegisters(uint8_t startReg, const uint8_t *buffer, uint8_t count);

    /**
     * @brief Modify specific bits in a register
     * @param reg Register address
     * @param mask Bit mask for bits to modify
     * @param value New value for masked bits
     * @return RV8803Error::OK on success
     */
    RV8803Error modifyRegister(uint8_t reg, uint8_t mask, uint8_t value);

    // =========================================================================
    // Private Utility Functions
    // =========================================================================

    /**
     * @brief Convert BCD to binary
     * @param bcd BCD value
     * @return Binary value
     */
    static uint8_t bcdToBin(uint8_t bcd);

    /**
     * @brief Convert binary to BCD
     * @param bin Binary value
     * @return BCD value
     */
    static uint8_t binToBcd(uint8_t bin);

    /**
     * @brief Validate DateTime values
     * @param dt DateTime to validate
     * @return true if all values are in valid range
     */
    static bool validateDateTime(const RV8803DateTime &dt);

    /**
     * @brief Check if year is a leap year
     * @param year Year (0-99 representing 2000-2099)
     * @return true if leap year
     */
    static bool isLeapYear(uint8_t year);

    /**
     * @brief Get days in month
     * @param month Month (1-12)
     * @param year Year (0-99 representing 2000-2099)
     * @return Number of days in the month
     */
    static uint8_t daysInMonth(uint8_t month, uint8_t year);

    /**
     * @brief Calculate weekday from date (Zeller's congruence)
     * @param year Year (0-99 representing 2000-2099)
     * @param month Month (1-12)
     * @param date Day of month (1-31)
     * @return Weekday as bit value (1=Sunday, 2=Monday, etc.)
     */
    static uint8_t calculateWeekday(uint8_t year, uint8_t month, uint8_t date);
};
