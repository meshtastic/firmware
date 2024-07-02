#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <INA3221.h>

class INA3221Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    INA3221 ina3221 = INA3221(INA3221_ADDR42_SDA);

    // channel to report voltage/current for environment metrics
    ina3221_ch_t ENV_CH = INA3221_CH1;

    // channel to report battery voltage for device_battery_ina_address
    ina3221_ch_t BAT_CH = INA3221_CH1;

    // get a single measurement for a channel
    struct _INA3221Measurement getMeasurement(ina3221_ch_t ch);

    // get all measurements for all channels
    struct _INA3221Measurements getMeasurements();

    bool getEnvironmentMetrics(meshtastic_Telemetry *measurement);
    bool getPowerMetrics(meshtastic_Telemetry *measurement);

  protected:
    void setup() override;

  public:
    INA3221Sensor();
    int32_t runOnce() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
};

struct _INA3221Measurement {
    float voltage;
    float current;
};

struct _INA3221Measurements {
    // INA3221 has 3 channels
    struct _INA3221Measurement measurements[3];
};

#endif