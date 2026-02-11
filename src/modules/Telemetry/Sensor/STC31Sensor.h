#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cStc3x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2cStc3x.h>

class STC31Sensor : public TelemetrySensor
{
  private:
    SensirionI2cStc3x stc3x;
    TwoWire *_bus{};
    uint8_t _address{};
    bool binaryGasConfigured = false;

    bool configureBinaryGas();
    bool setHumidityCompensation();

  public:
    STC31Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
