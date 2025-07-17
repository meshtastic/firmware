#ifndef SFA30SENSOR_H

#define SFA30SENSOR_H


#include "Wire.h"
#include "configuration.h"

// #if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Sensirion_SFA30>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "SensirionI2CSfa3x.h"

#ifndef SENSIRION_I2C_CLOCK
#define SENSIRION_I2C_CLOCK 100000
#endif

class SFA30Sensor : public TelemetrySensor
{
  private:
     SensirionI2CSfa3x sfa30;
      TwoWire *wire;

  protected:
    virtual void setup() override;

  public:
    SFA30Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

// #endif

#endif /* end of include guard: SFA30SENSOR_H */
