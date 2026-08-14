#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd30.h>)

#include "../detect/ReClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CO2Sensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd30.h>

#define SCD30_I2C_CLOCK_SPEED 100000

class SCD30Sensor : public TelemetrySensor, public CO2CalibrationSensor
{
  private:
    SensirionI2cScd30 scd30;
#ifdef SCD30_I2C_CLOCK_SPEED
    ReClockI2C reClockI2C;
#endif

    bool performFRC(uint16_t targetCO2);
    bool setASC(bool ascEnabled);
    bool getASC(uint16_t &ascEnabled);
    bool setTemperature(float tempReference);
    bool getAltitude(uint16_t &altitude);
    bool setAltitude(uint16_t altitude);
    bool softReset(); //
    bool setMeasurementInterval(uint16_t measInterval);
    bool getMeasurementInterval(uint16_t &measInterval);
    bool startMeasurement();
    bool stopMeasurement();

    // CO2CalibrationSensor overrides - thin wrappers, shared with SCD4XSensor and
    // the CO2-capable SEN6X variants via CO2CalibrationSensor::handleCo2AdminRequest().
    // SCD30 has no ambient-pressure command or calibration-history factory reset, so
    // those two are left at CO2CalibrationSensor's default (unsupported) implementation.
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
    bool co2SetAltitude(uint32_t altitude) override
    {
        return altitude <= UINT16_MAX && setAltitude(static_cast<uint16_t>(altitude));
    }

    // Parameters
    uint16_t ascActive = 1;
    uint16_t measurementInterval = 2;

  public:
    SCD30Sensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    enum SCD30State { SCD30_OFF, SCD30_IDLE, SCD30_MEASUREMENT };
    SCD30State state = SCD30_OFF;

    virtual bool isActive() override;

    virtual void sleep() override;      // Stops measurement (measurement -> idle)
    virtual uint32_t wakeUp() override; // Starts measurement (idle -> measurement)
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif
