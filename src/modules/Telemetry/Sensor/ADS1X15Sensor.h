#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_ADS1X15.h>)

#include "../detect/ReClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_ADS1X15.h>

#define ADS1X15_I2C_CLOCK_SPEED 100000
// ADS1X15 has no practical way to be detected. Use this to toggle
// between ADS1015 (0) or ADS1115 (1)
#ifndef MESHTASTIC_ADC_ADS1115
#define MESHTASTIC_ADC_ADS1115 1
#endif

class ADS1X15Sensor : public TelemetrySensor
{
  private:
#if MESHTASTIC_ADC_ADS1115
    Adafruit_ADS1115 ads1x15{};
#else
    Adafruit_ADS1015 ads1x15{};
#endif

#ifdef ADS1X15_I2C_CLOCK_SPEED
    ReClockI2C reClockI2C;
#endif
    ScanI2C::DeviceType _deviceType{};

    // get a single measurement for a channel
    struct _ADS1X15Measurement getMeasurement(uint8_t ch);

    // get all measurements for all channels
    struct _ADS1X15Measurements getMeasurements();

  public:
    ADS1X15Sensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
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
