# New Telemetry Sensor

Guide for adding a new I2C telemetry sensor driver to Meshtastic firmware.

## Overview

Telemetry sensors live in `src/modules/Telemetry/Sensor/`. There are 50+ existing drivers organized by measurement type. Each sensor integrates with one of the telemetry modules:

- **EnvironmentTelemetryModule** — Temperature, humidity, pressure, gas, light
- **AirQualityTelemetryModule** — Particulate matter, VOCs
- **PowerTelemetryModule** — Voltage, current, power monitoring
- **HealthTelemetryModule** — Heart rate, SpO2, body temperature

## Sensor Driver Pattern

Each sensor has a `.h` and `.cpp` file pair following this pattern:

```cpp
// src/modules/Telemetry/Sensor/MySensor.h
#pragma once
#include "TelemetrySensor.h"
#include <MySensorLibrary.h>  // Arduino/PlatformIO library

class MySensor : virtual public TelemetrySensor
{
  private:
    MySensorLibrary sensor;

  public:
    MySensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MY_SENSOR, "MySensor") {}

    // Initialize sensor hardware. Return true on success.
    virtual void setup() override;

    // Read sensor data into the telemetry protobuf. Return true on success.
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};
```

```cpp
// src/modules/Telemetry/Sensor/MySensor.cpp
#include "MySensor.h"
#include "TelemetrySensor.h"

void MySensor::setup()
{
    sensor.begin();
    // Configure sensor parameters...
}

bool MySensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Read from hardware
    float value = sensor.readValue();

    // Populate the appropriate protobuf variant
    measurement->variant.environment_metrics.temperature = value;
    // ... other fields ...

    return true;
}
```

## I2C Address Registration

Register the sensor's I2C address(es) in `src/detect/ScanI2C` so it's auto-detected at boot:

1. Add a `DeviceType` enum entry in `src/detect/ScanI2C.h`
2. Add the I2C address mapping in `src/detect/ScanI2CTwoWire.cpp`

The scan runs at boot and populates a device map that telemetry modules use to decide which sensors to initialize.

## Protobuf Fields

If the sensor provides data not covered by existing telemetry fields:

1. Add fields to the appropriate message in `protobufs/meshtastic/telemetry.proto`:
   - `EnvironmentMetrics` — Environmental measurements
   - `AirQualityMetrics` — Air quality data
   - `PowerMetrics` — Power/energy data
   - `HealthMetrics` — Health/biometric data
2. Add a `.options` constraint if needed (field sizes for nanopb)
3. Regenerate: `bin/regen-protos.sh`

## Sensor Type Enum

Add the sensor to `meshtastic_TelemetrySensorType` enum in `protobufs/meshtastic/telemetry.proto`:

```protobuf
enum TelemetrySensorType {
    // ... existing entries ...
    MY_SENSOR = XX;
}
```

## Integration with Telemetry Module

Wire the sensor into the appropriate telemetry module. For environment sensors, this is typically in `src/modules/Telemetry/EnvironmentTelemetry.cpp`:

1. Include the sensor header
2. Add initialization in `setupSensor()` guarded by detection results
3. Call `getMetrics()` in the measurement collection path

Example pattern from existing sensors:

```cpp
#include "Sensor/MySensor.h"

MySensor mySensor;

// In setup:
if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MY_SENSOR].first > 0) {
    mySensor.setup();
}

// In measurement collection:
if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MY_SENSOR].first > 0) {
    mySensor.getMetrics(&measurement);
}
```

## Library Dependencies

If the sensor needs an external library, add it to the `lib_deps` in the relevant base platformio.ini configs:

```ini
lib_deps =
    ${env.lib_deps}
    mysensorlibrary@^1.0.0
```

Or use a conditional dependency if it's platform-specific.

## Unit Conversions

If the sensor reports values in non-standard units, use `src/modules/Telemetry/UnitConversions.h` for conversion helpers (e.g., Celsius ↔ Fahrenheit, hPa ↔ inHg).

## Checklist

- [ ] Create `src/modules/Telemetry/Sensor/MySensor.h` and `.cpp`
- [ ] Inherit from `TelemetrySensor` base class
- [ ] Implement `setup()` and `getMetrics()` methods
- [ ] Add `meshtastic_TelemetrySensorType` enum entry in `telemetry.proto`
- [ ] Add I2C address to `src/detect/ScanI2C` for auto-detection
- [ ] Add protobuf fields in `telemetry.proto` if new data types needed
- [ ] Regenerate protos: `bin/regen-protos.sh`
- [ ] Wire into the appropriate telemetry module (Environment/AirQuality/Power/Health)
- [ ] Add library dependency if external library required
- [ ] Test on hardware or native build
