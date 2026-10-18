#pragma once

#include "configuration.h"

/*
 * HX711 24-bit load-cell ADC (weight sensor).
 *
 * Board opt-in: the variant must define both pins:
 *   #define PIN_HX711_DATA 48       // in the board's variant.h
 *   #define PIN_HX711_SCK  47
 * The HX711's VCC/GND connect to 3V3/GND.
 *
 * Tare/calibration: send the set_scale admin message (0 = tare,
 * N = calibrate to N kg); values persist in /prefs/hx711.dat.
 *
 * Uses only the portable Arduino API (digitalRead/digitalWrite) - the HX711
 * protocol is self-clocked with millisecond-scale timing, so no
 * architecture-specific GPIO access is needed.
 */

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(PIN_HX711_DATA) && defined(PIN_HX711_SCK)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class HX711Sensor : public TelemetrySensor
{
  private:
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
