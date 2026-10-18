#pragma once

#include "configuration.h"

/*
 * Bit-banged 1-Wire DS18B20 temperature sensor.
 *
 * Board opt-in: the variant must define the data pin:
 *   #define PIN_DS18B20_DATA 4        // in the board's variant.h
 * A 4.7k pull-up between the data pin and 3V3 is required.
 *
 * Wiring (example, Heltec V3: PIN_DS18B20_DATA = GPIO4, J3 pin 15):
 *   Red    -> 3V3
 *   Black  -> GND
 *   Yellow -> data pin, with 4.7k pull-up to 3V3
 *
 * ESP32-only: the driver uses direct GPIO register access because Arduino
 * digitalWrite() overhead violates 1-Wire slot timing.
 *
 * Uses only the first sensor found on the bus (Skip ROM addressing).
 */

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_ESP32) && defined(PIN_DS18B20_DATA)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class DS18B20Sensor : public TelemetrySensor
{
  private:
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
