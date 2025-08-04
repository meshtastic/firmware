#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

#define SCD4X_I2C_CLOCK_SPEED 100000

class SCD4XSensor : public TelemetrySensor
{
  private:
    SensirionI2cScd4x scd4x;
    TwoWire* bus;
    uint8_t address;

    bool performFRC(uint32_t targetCO2);
    bool setASCBaseline(uint32_t targetCO2);
    bool getASC(uint16_t &ascEnabled);
    bool setASC(bool ascEnabled);
    bool setTemperature(float tempReference);
    bool getAltitude(uint16_t &altitude);
    bool setAltitude(uint32_t altitude);
    bool getAmbientPressure(uint32_t &ambientPressure);
    bool setAmbientPressure(uint32_t ambientPressure);
#ifdef SCD4X_I2C_CLOCK_SPEED
    uint32_t setI2CClock(uint32_t currentClock);
#endif
    bool factoryReset();
    bool setPowerMode(bool _lowPower);
    bool startMeasurement();
    bool stopMeasurement();

    // Parameters
    uint16_t ascActive;
    bool lowPower = true;

  protected:
    virtual void setup() override;

  public:
    SCD4XSensor();
    enum SCD4XState { SCD4X_OFF, SCD4X_IDLE, SCD4X_MEASUREMENT };
    SCD4XState state = SCD4X_OFF;
    SCD4xSensorVariant sensorVariant;
    bool sleep();
    bool wakeUp();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif