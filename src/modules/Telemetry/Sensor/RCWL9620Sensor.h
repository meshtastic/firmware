#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Wire.h>

class RCWL9620Sensor : public TelemetrySensor
{
  private:
    uint8_t _addr = 0x57;
    TwoWire *_wire = &Wire;
    uint8_t _scl = -1;
    uint8_t _sda = -1;
    uint32_t _speed = 200000UL;

  protected:
    virtual void setup() override;
    void begin(TwoWire *wire = &Wire, uint8_t addr = 0x57, uint8_t sda = -1, uint8_t scl = -1, uint32_t speed = 200000UL);
    float getDistance();

  public:
    RCWL9620Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif