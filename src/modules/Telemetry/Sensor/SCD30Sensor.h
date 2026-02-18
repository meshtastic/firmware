#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd30.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd30.h>

#define SCD30_I2C_CLOCK_SPEED 100000

class SCD30Sensor : public TelemetrySensor
{
  private:
    SensirionI2cScd30 scd30;
    TwoWire *_bus{};
    uint8_t _address{};

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