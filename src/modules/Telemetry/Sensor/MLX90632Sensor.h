#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SparkFun_MLX90632_Arduino_Library.h>

class MLX90632Sensor : public TelemetrySensor
{
  private:
    MLX90632 mlx = MLX90632();

  protected:
    virtual void setup() override;

  public:
    MLX90632Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif