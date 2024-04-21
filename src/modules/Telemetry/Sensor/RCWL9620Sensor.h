#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_I2CDevice.h>

class RCWL9620Sensor : public TelemetrySensor
{
  private:
    uint8_t _addr;
    TwoWire *_wire;

  protected:
    virtual void setup() override;
    bool begin(uint8_t addr = 0x57, TwoWire *wire = &Wire);
    Adafruit_I2CDevice *i2c_dev = NULL;
    float getDistance();

  public:
    RCWL9620Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};