#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_ADS1X15.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_ADS1X15.h>

#define ADS1X15_I2C_CLOCK_SPEED 100000

class ADS1X15Sensor : public TelemetrySensor
{
  private:
    Adafruit_ADS1X15 ads1x15;
    TwoWire * bus;
    uint8_t address;

    // get a single measurement for a channel
    struct _ADS1X15Measurement getMeasurement(uint8_t ch);

    // get all measurements for all channels
    struct _ADS1X15Measurements getMeasurements();

  protected:
    virtual void setup() override;

  public:
    ADS1X15Sensor(meshtastic_TelemetrySensorType sensorType = meshtastic_TelemetrySensorType_ADS1X15);
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

struct _ADS1X15Measurement {
    float voltage;
};

struct _ADS1X15Measurements {
    // ADS1X15 has 4 channels
    struct _ADS1X15Measurement measurements[4];
};

#endif