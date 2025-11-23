#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "TelemetrySensor.h"
#include <DallasTemperature.h>
#include <OneWire.h>

class DS18B20Sensor : public TelemetrySensor {
  private:
    OneWire *oneWire;
    DallasTemperature *sensors;
    uint8_t pin;

  public:
    DS18B20Sensor();
    virtual int32_t runOnce() override;
    virtual void setup() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif