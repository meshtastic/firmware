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
    void begin(TwoWire *wire = &Wire, uint8_t addr = 0x66);
    float getStaticRadiation();

  public:
    CGRadSensSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif