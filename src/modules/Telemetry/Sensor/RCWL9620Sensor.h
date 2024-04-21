#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Wire.h>

class RCWL9620Sensor : public TelemetrySensor
{
  private:
    uint8_t _addr;
    TwoWire *_wire;
    uint8_t _scl;
    uint8_t _sda;
    uint8_t _speed;

  protected:
    virtual void setup() override;
    void begin(TwoWire *wire = &Wire, uint8_t addr = 0x57, uint8_t sda = -1, uint8_t scl = -1, uint32_t speed = 200000L);
    float getDistance();

  public:
    RCWL9620Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};