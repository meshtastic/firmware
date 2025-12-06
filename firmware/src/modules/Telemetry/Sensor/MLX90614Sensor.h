#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_MLX90614.h>)
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_MLX90614.h>

#define MLX90614_EMISSIVITY 0.98 // human skin

class MLX90614Sensor : public TelemetrySensor
{
  private:
    Adafruit_MLX90614 mlx = Adafruit_MLX90614();

  protected:
    virtual void setup() override;

  public:
    MLX90614Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
