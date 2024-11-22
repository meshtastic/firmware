/*
 *  Support for the ClimateGuard RadSens Dosimeter
 *  A fun and educational sensor for Meshtastic; not for safety critical applications.
 */
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Wire.h>

class CGRadSensSensor : public TelemetrySensor
{
  private:
    uint8_t _addr = 0x66;
    TwoWire *_wire = &Wire;

  protected:
    virtual void setup() override;
    void begin(TwoWire *wire = &Wire, uint8_t addr = 0x66);
    float getStaticRadiation();

  public:
    CGRadSensSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif