#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_MLX90632_Arduino_Library.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SparkFun_MLX90632_Arduino_Library.h>

class MLX90632Sensor : public TelemetrySensor
{
  private:
    MLX90632 mlx = MLX90632();

  public:
    MLX90632Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif