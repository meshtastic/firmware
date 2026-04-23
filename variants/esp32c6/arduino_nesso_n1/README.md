# Arduino Nesso N1 Support Notes

This document summarizes the Nesso N1-specific firmware support and the current
known limitations.

## Supported hardware

- ESP32-C6 based Nesso N1 board variant: `arduino-nesso-n1`
- SX1262 LoRa radio
- TFT display
- Touchscreen, enabled by default
- M5Stack CardKB through the existing I2C keyboard path
- Environmental telemetry through the Grove/I2C port, for sensors already
  supported by Meshtastic

## Build options

The touchscreen can be disabled at build time:

```ini
-D HAS_TOUCHSCREEN=0
```

The default variant keeps it enabled:

```cpp
#define HAS_TOUCHSCREEN 1
```

The define is intentionally overrideable from PlatformIO build flags.

## I2C address notes

The Nesso touchscreen uses I2C address `0x38`, which is also the default AHT10
address in Meshtastic. For this board, `0x38` is ignored by the AHT10
auto-detection path to avoid registering the touchscreen as a broken AHT10
environment sensor.

CardKB uses the existing Meshtastic I2C keyboard detection and does not require a
Nesso-specific driver.

## LoRa notes

For JP region testing, the board has been validated with the app's LoRa
frequency slot set explicitly. A mismatch between the configured slot and the
other mesh nodes can look like successful local TX with no network join.

## Environmental sensor notes

M5Stack Unit ENV and Unit ENV Pro can work through existing Meshtastic sensor
drivers when their onboard sensors are supported.

M5Stack Unit ENV III contains an SHT30 temperature/humidity sensor and a QMP6988
pressure sensor. SHT30 support exists through the generic `SHTXXSensor`, but ENV
III may still fail today because the unsupported QMP6988 at address `0x70` can
be detected as `SHTXX` and overwrite the SHT30 detected at `0x44`. Treat that as
a separate generic I2C sensor detection issue, not a Nesso-only issue.

QMP6988 pressure readings are not currently supported unless a QMP6988 driver is
added.
