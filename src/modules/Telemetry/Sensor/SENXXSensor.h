#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../detect/ReClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CO2Sensor.h"
#include "TelemetrySensor.h"
#include "Wire.h"
#include "gps/RTC.h"

/*
Shared driver for Sensirion's SEN5X and SEN6X particulate-matter sensor families
(SEN50/54/55 and SEN62/63C/65/66/68/69C). All of these sensors speak the same
16-bit-command + CRC8-framed word I2C protocol (reset, product name, start/stop
measurement, data-ready, fan cleaning, VOC algorithm state, ...). The families
differ only in:
  - I2C address (SEN5X_ADDR 0x69 vs SEN6X_ADDR 0x6B)
  - which physical quantities a given model exposes (PM is universal; RHT, VOC,
    NOx, CO2 and HCHO are present on some models and not others)
  - the opcode used to read measured values (SEN5X always uses one fixed-format
    command; each SEN6X model has its own opcode returning only the words that
    model supports)
This class implements the shared protocol, state machine and admin handling
once. SEN5XSensor / SEN6XSensor (see SEN5XSensor.h / SEN6XSensor.h) are thin
subclasses that only supply the sensorType/sensorName identity.
*/
#define SENXX_PM_WARMUP_MS_1 15000
#define SENXX_PM_WARMUP_MS_2 30000
#define SENXX_POLL_INTERVAL 1000
#define SENXX_I2C_CLOCK_SPEED 100000
// How long a fan-cleaning cycle takes once started; polled via pendingForReadyMs()
// rather than blocked on, see SENXX_CLEANING in SENXXState.
#define SENXX_CLEANING_DURATION_MS 10500

/*
Time after which the co2 sensor in some SEN6X variants give stable data
*/
#define SEN6X_CO2_WARMUP_MS 24000
#define SENXX_VOC_VALID_TIME 600
#define SENXX_VOC_VALID_DATE 1514764800

/*
Time after which the sensor can go to sleep, as the warmup period has passed
and the VOCs sensor will is allowed to stop (although needs to recover the state
each time)
Note: for Testing 5' is enough. Sensirion recommends 1h
This can be bypassed completely if switching to low-power RHT/Gas mode and setting
SENXX_VOC_STATE_WARMUP_S 0
*/
#define SENXX_VOC_STATE_WARMUP_S 3600
#define SENXX_VOC_STATE_BUFFER_SIZE 8

/* Sensirion recommends taking a reading after 15 seconds,
if the Particle number reading is over 100#/cm3 the reading is OK,
but if it is lower wait until 30 seconds and take it again.
See: https://sensirion.com/resource/application_note/low_power_mode/sen5x
*/
#define SENXX_PN4P0_CONC_THD 100
#ifndef ONE_WEEK_IN_SECONDS
#define ONE_WEEK_IN_SECONDS 604800
#endif

// Commands shared identically by every SEN5X/SEN6X model
#define SENXX_RESET 0xD304
#define SENXX_GET_PRODUCT_NAME 0xD014
#define SENXX_GET_FIRMWARE_VERSION 0xD100
#define SENXX_START_MEASUREMENT 0x0021
#define SENXX_STOP_MEASUREMENT 0x0104
#define SENXX_READ_DATA_READY 0x0202
#define SENXX_START_FAN_CLEANING 0x5607
#define SENXX_RW_VOCS_STATE 0x6181

// SEN5X-only: low-power "RHT/Gas only" measurement mode and fixed-format read commands
#define SEN5X_START_MEASUREMENT_RHT_GAS 0x0037
#define SEN5X_READ_VALUES 0x03C4
#define SEN5X_READ_PM_VALUES 0x0413

// SEN6X-only: shared number-concentration read command (per-model measured-values
// opcode lives in readMeasuredValuesCmd, set once the model is known)
#define SEN6X_READ_NUMBER_CONCENTRATION_VALUES 0x0316

// Values the sensor reports when a reading is unavailable (same sentinels across
// the whole SEN5X/SEN6X family per Sensirion's datasheets)
#define SENXX_UINT_INVALID 0xFFFF
#define SENXX_INT_INVALID 0x7FFF

// Reply payload sizes in data bytes; the raw I2C transfer adds one CRC byte per
// 2-byte group, so requests are <size> + <size> / 2 raw bytes
#define SENXX_VERSION_BUFFER_SIZE 8
#define SENXX_PRODUCT_NAME_BUFFER_SIZE 32
#define SENXX_DATA_READY_BUFFER_SIZE 2
#define SEN5X_READ_VALUES_BUFFER_SIZE 16
#define SEN5X_READ_PM_BUFFER_SIZE 20

// SEN6X-only commands (all models: SEN62/63C/65/66/68/69C)
#define SEN6X_GET_SET_TEMP_OFFSET 0x60B2
#define SEN6X_READ_DEVICE_STATUS 0xD206
// SEN6X-only, CO2-capable models only (SEN63C/66/69C)
#define SEN6X_PERFORM_FORCED_CO2_RECAL 0x6707
#define SEN6X_CO2_FACTORY_RESET 0x6754
#define SEN6X_GET_SET_CO2_ASC 0x6711
#define SEN6X_GET_SET_AMBIENT_PRESSURE 0x6720
#define SEN6X_GET_SET_ALTITUDE 0x6736

struct _SENXXMeasurements {
    uint16_t pM1p0;
    uint16_t pM2p5;
    uint16_t pM4p0;
    uint16_t pM10p0;
    uint32_t pN0p5;
    uint32_t pN1p0;
    uint32_t pN2p5;
    uint32_t pN4p0;
    uint32_t pN10p0;
    float tSize;
    float humidity;
    float temperature;
    float vocIndex;
    float noxIndex;
    float co2;
    float hcho;
};

class SENXXSensor : public TelemetrySensor, public CO2CalibrationSensor
{
  protected:
    // Only subclasses (SEN5XSensor / SEN6XSensor) construct this; they supply the
    // proto sensorType/sensorName identity, everything else is auto-detected via
    // findModel() at probe/init time.
    SENXXSensor(meshtastic_TelemetrySensorType sensorType, const char *sensorName) : TelemetrySensor(sensorType, sensorName) {}

  private:
#ifdef SENXX_I2C_CLOCK_SPEED
    ReClockI2C reClockI2C;
#endif

    bool getVersion();
    float firmwareVer = -1;
    float hardwareVer = -1;
    float protocolVer = -1;
    bool findModel();

    enum SENXXmodel {
        SENXX_UNKNOWN = 0,
        // SEN5X family - I2C address SEN5X_ADDR (0x69)
        SEN50,
        SEN54,
        SEN55,
        // SEN6X family - I2C address SEN6X_ADDR (0x6B)
        SEN62,
        SEN63C,
        SEN65,
        SEN66,
        SEN68,
        SEN69C,
    };
    SENXXmodel model = SENXX_UNKNOWN;

    // True for any SEN6X-family model (SEN62/63C/65/66/68/69C)
    bool isSen6xFamily() { return model >= SEN62; }

    // Per-model capabilities, derived once in updateCapabilities() right after
    // findModel() succeeds. Every read/state routine below is written against
    // these flags rather than against individual model checks, so adding a new
    // family member only means extending findModel()/updateCapabilities().
    bool hasRHT = false;
    bool hasVOC = false;
    bool hasNOx = false;
    bool hasCO2 = false;
    bool hasHCHO = false;
    void updateCapabilities();

    // SEN6X: opcode for "Read Measured Values" - differs per model, see updateCapabilities()
    uint16_t readMeasuredValuesCmd = 0;

    // Device Status Register bit positions (SEN6X only - see datasheet Figure 7)
    static constexpr uint32_t SEN6X_STATUS_FAN_ERROR = 1u << 4;
    static constexpr uint32_t SEN6X_STATUS_RHT_ERROR = 1u << 6;
    static constexpr uint32_t SEN6X_STATUS_GAS_ERROR = 1u << 7;
    static constexpr uint32_t SEN6X_STATUS_CO2_2_ERROR = 1u << 9; // SEN66 only
    static constexpr uint32_t SEN6X_STATUS_HCHO_ERROR = 1u << 10;
    static constexpr uint32_t SEN6X_STATUS_PM_ERROR = 1u << 11;
    static constexpr uint32_t SEN6X_STATUS_CO2_1_ERROR = 1u << 12; // SEN63C/SEN69C only
    static constexpr uint32_t SEN6X_STATUS_FAN_SPEED_WARNING = 1u << 21;

    bool readDeviceStatus(uint32_t &statusFlags);
    void logDeviceStatus(uint32_t statusFlags);

    // Sets the SEN6X RHT temperature-offset compensation (slot 0, applied immediately)
    // from the most recently measured temperature vs. a known-good reference. Unlike
    // SCD4X/SCD30 there is no "get current offset" command to accumulate against, so
    // this simply computes offset = lastMeasuredTemperature - tempReference.
    bool setTemperatureOffset(float tempReference);

    // CO2CalibrationSensor overrides - only meaningful when hasCO2 (SEN63C/66/69C);
    // return false/no-op otherwise.
    bool co2PerformFRC(uint32_t targetCO2ppm) override;
    bool co2GetASC(bool &ascEnabled) override;
    bool co2SetASC(bool ascEnabled) override;
    bool co2SetAltitude(uint32_t altitude) override;
    bool co2SetAmbientPressure(uint32_t ambientPressurePa) override;
    bool co2FactoryReset() override;

    enum SENXXState {
        SENXX_OFF,
        SENXX_IDLE,
        SENXX_RHTGAS_ONLY, // SEN5X Only
        SENXX_MEASUREMENT,
        SENXX_MEASUREMENT_2,
        SENXX_CLEANING,
        SENXX_NOT_DETECTED
    };
    SENXXState state = SENXX_OFF;
    // Flag to work on one-shot (read and sleep), or continuous mode
    // Recommendation: if it has VOC / NOx, suggest NOT to use oneShot mode
    bool oneShotMode = true;
    void setMode(bool setOneShot);
    bool vocStateValid();

    // Tracks getRTCQuality() across calls so we can notice the moment a real clock
    // becomes available (e.g. the phone/WiFi/GPS sets it well after boot), rather than
    // only checking once in initDevice(). See checkRTCQualityImproved()/
    // reconcileTimeDependentState() for how this is used.
    RTCQuality lastRTCQuality = RTCQualityNone;
    bool checkRTCQualityImproved();
    void reconcileTimeDependentState(uint32_t now);

    bool sendCommand(uint16_t command);
    /**
     * @brief Send a command word followed by a data payload; a CRC byte is
     *        computed and inserted on the wire after every 2-byte pair.
     * @param command 16-bit command code, sent big-endian
     * @param buffer payload data bytes, without CRCs
     * @param byteNumber payload size in data bytes; must be even
     * @return true when the full transfer is written and acknowledged
     */
    bool sendCommand(uint16_t command, uint8_t *buffer, uint8_t byteNumber = 0);
    /**
     * @brief Read a reply, verifying and stripping the interleaved CRC bytes.
     * @param buffer destination for the data bytes (byteNumber * 2 / 3 of them)
     * @param byteNumber raw transfer size including CRCs; must be a multiple
     *        of 3 (2 data bytes + 1 CRC per group)
     * @return the number of data bytes written to buffer, or 0 on any error
     */
    uint8_t readBuffer(uint8_t *buffer, uint8_t byteNumber);
    uint8_t senxxCRC(const uint8_t *buffer);
    // Starts a fan-cleaning cycle and returns immediately (does not block for the
    // ~10.5s the cycle takes); pendingForReadyMs() polls SENXX_CLEANING to completion
    // and calls finishCleaning() once done.
    bool startCleaning();
    void finishCleaning();
    uint8_t getMeasurements();
    bool readPNValues(bool cumulative);
    bool readValues();

    // Actual wakeUp() logic, factored out so handleAdminMessage() can resume
    // measurement after a calibration pause without nesting a second I2C-clock
    // guard inside its own (see ReClockI2CGuard's reentrancy note).
    uint32_t wakeUpInternal();

    // Monotonic (millis()) timers for warmup/poll intervals. Deliberately not
    // wall-clock (getTime()) based: getTime() can jump discontinuously the moment the RTC
    // quality improves mid-session (see checkRTCQualityImproved()), which would corrupt
    // these short elapsed-time computations. millis() is immune to that and wraps only every ~49 days.
    uint32_t pmMeasureStarted = 0;
    uint32_t rhtGasMeasureStarted = 0;
    uint32_t lastDataPoll = 0;
    uint32_t cleaningStarted = 0;
    _SENXXMeasurements senxxmeasurement{};

    bool idle(bool checkState = true);

  protected:
    // Store status of the sensor in this file. SEN5X and SEN6X keep separate prefs
    // files/proto messages so existing SEN5X saved state is unaffected.
    const char *senXXStateFileName = nullptr;
    meshtastic_SEN5XState sen5xstate = meshtastic_SEN5XState_init_zero;
    meshtastic_SEN6XState sen6xstate = meshtastic_SEN6XState_init_zero;

    bool loadState();
    bool saveState();

    // Cleaning State
    uint32_t lastCleaning = 0;
    bool lastCleaningValid = false;

    // VOC State
    uint8_t vocState[SENXX_VOC_STATE_BUFFER_SIZE]{};
    uint32_t vocTime = 0;
    bool vocValid = false;

    bool vocStateFromSensor();
    bool vocStateToSensor();
    bool vocStateStable();
    bool vocStateRecent(uint32_t now);

  public:
    bool probe(TwoWire *bus, uint8_t address, ScanI2C::I2CPort port);
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    virtual bool isActive() override;
    virtual void sleep() override;
    virtual uint32_t wakeUp() override;
    virtual bool canSleep() override { return true; }
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;

    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif
