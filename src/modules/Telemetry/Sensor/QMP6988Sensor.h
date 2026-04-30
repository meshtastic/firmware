#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class QMP6988Sensor : public TelemetrySensor
{
  public:
    QMP6988Sensor();
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

  private:
    struct CalibrationData {
        int32_t a0;
        int16_t a1;
        int16_t a2;
        int32_t b00;
        int16_t bt1;
        int16_t bt2;
        int16_t bp1;
        int16_t b11;
        int16_t bp2;
        int16_t b12;
        int16_t b21;
        int16_t bp3;
    };

    struct IntegerCalibrationData {
        int32_t a0;
        int32_t b00;
        int32_t a1;
        int32_t a2;
        int64_t bt1;
        int64_t bt2;
        int64_t bp1;
        int64_t b11;
        int64_t bp2;
        int64_t b12;
        int64_t b21;
        int64_t bp3;
    };

    bool readBytes(uint8_t reg, uint8_t *buffer, uint8_t length);
    bool writeByte(uint8_t reg, uint8_t value);
    bool readByte(uint8_t reg, uint8_t &value);
    bool reset();
    bool readCalibrationData();
    void setPowerMode(uint8_t powerMode);
    void setFilter(uint8_t filter);
    void setOversamplingPressure(uint8_t oversampling);
    void setOversamplingTemperature(uint8_t oversampling);
    int16_t convertTemperature(int32_t rawTemperature) const;
    int32_t convertPressure(int32_t rawPressure, int16_t compensatedTemperature) const;
    bool readRawMeasurement(int32_t &rawPressure, int32_t &rawTemperature);

    TwoWire *bus{};
    uint8_t address{};
    CalibrationData calibration{};
    IntegerCalibrationData integerCalibration{};
};

#endif
