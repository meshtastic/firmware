#pragma once

// INA3221 channel aliases (zero-based: 0 = CH1, 1 = CH2, 2 = CH3).
// Defined before configuration.h so variant.h can use them in INA3221_ENV_CH / INA3221_BAT_CH.
#define INA3221_CH1 0
#define INA3221_CH2 1
#define INA3221_CH3 2

#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_INA_SENSORS

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CurrentSensor.h"
#include "INAFamily.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"

static_assert(INA3221_ENV_CH >= 0 && INA3221_ENV_CH <= 2, "INA3221_ENV_CH must be 0, 1, or 2");
static_assert(INA3221_BAT_CH >= 0 && INA3221_BAT_CH <= 2, "INA3221_BAT_CH must be 0, 1, or 2");

class INAFamilySensor : public TelemetrySensor, VoltageSensor, CurrentSensor
{
  public:
    INAFamilySensor(meshtastic_TelemetrySensorType type, const char *name, const InaChip &chip, const float (&shuntR)[3],
                    uint8_t envChannel, uint8_t batteryChannel);

    int32_t runOnce() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    uint16_t getBusVoltageMv() override;
    int16_t getCurrentMa() override;

  private:
    bool reset();
    bool readReg(uint8_t reg, uint8_t bytes, uint32_t &out);
    bool readChannel(uint8_t channel, InaReading &out);

    bool getEnvironmentMetrics(meshtastic_Telemetry *measurement);
    bool getPowerMetrics(meshtastic_Telemetry *measurement);
    void clearEnvironmentMetrics(meshtastic_Telemetry *measurement);

    const InaChip &chip;
    float shuntR[3];
    TwoWire *wire = nullptr;
    uint8_t address = 0;
    uint8_t envChannel;
    uint8_t batteryChannel;
};

#endif
