#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CurrentSensor.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <INA228.h>

class INA228Sensor : public TelemetrySensor, VoltageSensor, CurrentSensor
{
  private:
    uint8_t _addr = INA_ADDR;
    TwoWire *_wire = &Wire;
    INA228 ina228 = INA228(_addr, _wire);

    bool getEnvironmentMetrics(meshtastic_Telemetry *measurement);
    bool getPowerMetrics(meshtastic_Telemetry *measurement);
    void calibrate();

  protected:
    virtual void setup() override;
    void begin(TwoWire *wire = &Wire, uint8_t addr = INA_ADDR);

  public:
    INA228Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
    virtual int16_t getCurrentMa() override;
};

#endif
