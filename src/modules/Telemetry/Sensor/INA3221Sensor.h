// INA3221 channel aliases (zero-based: 0 = CH1, 1 = CH2, 2 = CH3).
// Defined before configuration.h so variant.h can use them in INA3221_ENV_CH / INA3221_BAT_CH.
#define INA3221_CH1 0
#define INA3221_CH2 1
#define INA3221_CH3 2

#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<INA3221.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CurrentSensor.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <INA3221.h>

#ifndef INA3221_ENV_CH
#define INA3221_ENV_CH INA3221_CH1 // channel to report in environment metrics (default: CH1)
#endif

#ifndef INA3221_BAT_CH
#define INA3221_BAT_CH INA3221_CH1 // channel for device_battery_ina_address (default: CH1)
#endif

class INA3221Sensor : public TelemetrySensor, VoltageSensor, CurrentSensor
{
  private:
    // Placeholder constructor; re-initialised with correct address and Wire in runOnce().
    INA3221 ina3221 = INA3221(INA3221_ADDR);

    // channel to report voltage/current for environment metrics
    static const uint8_t ENV_CH = INA3221_ENV_CH;
    static_assert(INA3221_ENV_CH >= 0 && INA3221_ENV_CH <= 2, "INA3221_ENV_CH must be 0, 1, or 2");

    // channel to report battery voltage for device_battery_ina_address
    static const uint8_t BAT_CH = INA3221_BAT_CH;
    static_assert(INA3221_BAT_CH >= 0 && INA3221_BAT_CH <= 2, "INA3221_BAT_CH must be 0, 1, or 2");

    // get a single measurement for a channel
    struct _INA3221Measurement getMeasurement(uint8_t ch);

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
    virtual int16_t getCurrentMa() override;

    // Raw register reads (bits [15:3] right-shifted), no conversion applied.
    int16_t getRawBusVoltage(uint8_t ch);
    int16_t getRawShuntCurrent(uint8_t ch);
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