#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HELTEC_V3)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

/**
 * Bit-banged 1-Wire DS18B20 temperature sensor.
 *
 * Wiring (Heltec V3):
 *   Red    -> 3V3   (J3 pin 2)
 *   Black  -> GND   (J3 pin 1)
 *   Yellow -> GPIO4 (J3 pin 15), with 4.7k pull-up between GPIO4 and 3V3
 *
 * Uses only the first sensor found on the bus (Skip ROM addressing).
 */
class DS18B20Sensor : public TelemetrySensor
{
  private:
    static constexpr int ONE_WIRE_PIN = 4; // GPIO4 on Heltec V3 (J3 pin 15)

    // Returned when the 1-Wire transaction fails or scratchpad CRC mismatches
    static constexpr float INVALID_TEMPERATURE = -1000.0f;

    // --- Low-level 1-Wire primitives (bit-banged) ---
    bool oneWireReset();
    void oneWireWriteByte(uint8_t b);
    uint8_t oneWireReadByte();
    bool oneWireReadBit();

    // Read scratchpad with CRC check; returns INVALID_TEMPERATURE on failure
    float readTemperature();

  public:
    DS18B20Sensor();

    bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
