#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HELTEC_V3)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class HX711Sensor : public TelemetrySensor
{
  private:
    static constexpr int DATA_PIN = 48;
    static constexpr int CLOCK_PIN = 47;

    // Number of raw ADC samples averaged per measurement
    static constexpr uint8_t SAMPLES_TO_AVERAGE = 8;

    // Persisted calibration values (stored in flash via protobuf config file)
    int32_t zeroOffset = 0;
    float calibrationFactor = 1.0f; // raw counts per kg

    const char *hx711ConfigFileName = "/prefs/hx711.dat";

    bool saveCalibrationData();
    bool loadCalibrationData();

    bool waitReady(uint32_t timeoutMs);
    bool readRaw(int32_t &value, uint32_t timeoutMs);
    bool readAverage(int32_t &average, uint32_t timeoutMs);

  public:
    HX711Sensor();

    bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;

    void tare();
    void calibrate(float weightKg);

    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif
