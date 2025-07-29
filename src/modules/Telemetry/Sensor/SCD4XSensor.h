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
    bool setAmbientPressure(uint32_t ambientPressure);
    bool restoreClock(uint32_t currentClock);
    bool factoryReset();

    // Parameters
    uint16_t ascActive;

  protected:
    virtual void setup() override;

  public:
    SCD4XSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif