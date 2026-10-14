#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../detect/ReClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CO2Sensor.h"
#include "TelemetrySensor.h"
#include "gps/RTC.h"
#include <SensirionI2cScd4x.h>

// Max speed 400kHz
#define SCD4X_I2C_CLOCK_SPEED 400000
#define SCD4X_WARMUP_MS 5000
#define SCD4X_MAX_RETRIES 3

class SCD4XSensor : public TelemetrySensor, public CO2CalibrationSensor
{
  private:
    SensirionI2cScd4x scd4x;
#ifdef SCD4X_I2C_CLOCK_SPEED
    ReClockI2C reClockI2C;
#endif

    bool performFRC(uint32_t targetCO2);
    bool setASCBaseline(uint32_t targetCO2);
    bool getASC(uint16_t &ascEnabled);
    bool setASC(bool ascEnabled);
    bool setTemperature(float tempReference);
    bool getAltitude(uint16_t &altitude);
    bool setAltitude(uint32_t altitude);
    bool getAmbientPressure(uint32_t &ambientPressure);
    bool setAmbientPressure(uint32_t ambientPressure);
    bool factoryReset();
    bool setPowerMode(bool _lowPower);
    bool startMeasurement();
    bool stopMeasurement();

    // CO2CalibrationSensor overrides - thin wrappers around the methods above,
    // shared with SCD30Sensor and the CO2-capable SEN6X variants via
    // CO2CalibrationSensor::handleCo2AdminRequest().
    bool co2PerformFRC(uint32_t targetCO2ppm) override
    {
        return targetCO2ppm <= UINT16_MAX && performFRC(static_cast<uint16_t>(targetCO2ppm));
    }
    bool co2GetASC(bool &ascEnabled) override
    {
        uint16_t v = 0;
        bool ok = getASC(v);
        ascEnabled = v != 0;
        return ok;
    }
    bool co2SetASC(bool ascEnabled) override { return setASC(ascEnabled); }
    bool co2SetASCBaseline(uint32_t targetCO2ppm) override
    {
        return targetCO2ppm <= UINT16_MAX && setASCBaseline(static_cast<uint16_t>(targetCO2ppm));
    }
    bool co2SetAltitude(uint32_t altitude) override
    {
        if (altitude > 3000)
            return false;
        return altitude <= UINT16_MAX && setAltitude(static_cast<uint16_t>(altitude));
    }
    bool co2SetAmbientPressure(uint32_t ambientPressurePa) override
    {
        if (ambientPressurePa < 70000 || ambientPressurePa > 120000)
            return false;
        return setAmbientPressure(ambientPressurePa);
    }
    bool co2FactoryReset() override { return factoryReset(); }

    uint16_t ascActive = 1;
    // low power measurement mode (on sensirion side). Disables sleep mode
    // Improvement and testing needed for timings
    bool lowPower = true;
    // millis()-based, not wall-clock: this only measures in-session warmup elapsed time,
    // and getTime() can jump discontinuously when RTC quality improves mid-session.
    uint32_t co2MeasureStarted = 0;

  public:
    SCD4XSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    enum SCD4XState { SCD4X_OFF, SCD4X_IDLE, SCD4X_MEASUREMENT };
    SCD4XState state = SCD4X_OFF;
    SCD4xSensorVariant sensorVariant{};

    virtual bool isActive() override;

    virtual void sleep() override;      // Stops measurement (measurement -> idle)
    virtual uint32_t wakeUp() override; // Starts measurement (idle -> measurement)
    bool powerDown();                   // Powers down sensor (idle -> power-off)
    bool powerUp();                     // Powers the sensor (power-off -> idle)
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif
