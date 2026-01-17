# RV-8803-C7 RTC Driver

A safety-critical driver for the Micro Crystal RV-8803-C7 Real-Time Clock module, designed following **NASA's Power of 10 Rules** for safety-critical embedded systems.

## Features

- **Full RV-8803-C7 Feature Set**
  - Time/date with hundredths precision
  - Configurable alarm with multiple match criteria
  - Countdown timer with interrupt support
  - External event timestamp capture
  - Clock output (32.768kHz, 1.024kHz, 1Hz)
  - Temperature-compensated calibration (±3ppm)
  - 1 byte user RAM

- **Wear-Leveling Protection**
  - Configurable time update threshold (default 5 minutes)
  - Only updates RTC when time difference exceeds threshold
  - Reduces unnecessary writes to extend RTC lifespan

- **Meshtastic Integration**
  - Bridges with existing RTCQuality system
  - Drop-in replacement for other RTC modules
  - Threshold-based updates for mesh-sourced time

## NASA Power of 10 Rules Compliance

This driver strictly follows NASA JPL's coding rules for safety-critical systems:

| Rule | Description | Implementation |
|------|-------------|----------------|
| 1 | Simple control flow | No goto, setjmp/longjmp, or recursion |
| 2 | Fixed loop bounds | All loops bounded (MAX_I2C_RETRIES=3, etc.) |
| 3 | No dynamic allocation | Static allocation after init only |
| 4 | Function size | All functions ≤60 lines |
| 5 | Assertions | 2+ assertions per function (RV8803_ASSERT) |
| 6 | Minimal scope | Variables at smallest possible scope |
| 7 | Check returns | All I2C operations return RV8803Error |
| 8 | Limited preprocessor | Namespaced constants, minimal macros |
| 9 | Restricted pointers | Validated before use, no arithmetic |
| 10 | Warnings enabled | Designed for -Wall -Wextra clean compile |

## Files

```
src/takeover/RTC/
├── RV8803.h              # Driver class definition
├── RV8803.cpp            # Driver implementation
├── RV8803Integration.h   # Meshtastic integration hooks
├── RV8803Integration.cpp # Integration implementation
└── README.md             # This file
```

## Quick Start

### Basic Usage

```cpp
#include "takeover/RTC/RV8803.h"

RV8803 rtc;

void setup() {
    Wire.begin();

    // Initialize with default 5-minute threshold
    if (rtc.begin(Wire) == RV8803Error::OK) {
        Serial.println("RV8803 initialized");
    }
}

void loop() {
    RV8803DateTime dt;
    if (rtc.getDateTime(dt) == RV8803Error::OK) {
        Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
            2000 + dt.year, dt.month, dt.date,
            dt.hours, dt.minutes, dt.seconds);
    }
    delay(1000);
}
```

### Setting Time with Threshold

```cpp
// Only updates if difference > 5 minutes (300 seconds)
uint32_t newEpoch = 1704067200; // 2024-01-01 00:00:00
uint32_t delta;

RV8803Error err = rtc.updateIfDelta(newEpoch, delta);

if (err == RV8803Error::OK) {
    Serial.printf("Time updated, was off by %lu seconds\n", delta);
} else if (err == RV8803Error::THRESHOLD_NOT_MET) {
    Serial.printf("Time accurate within threshold (delta=%lu)\n", delta);
}
```

### Meshtastic Integration

```cpp
#include "takeover/RTC/RV8803Integration.h"

// In main.cpp setup():
void setup() {
    // ... I2C scanning ...

    // Initialize RV8803 if present
    if (initRV8803(i2cScanner)) {
        // Read initial time into system
        readFromRV8803();
    }
}

// In PositionModule.cpp when receiving mesh time:
void PositionModule::trySetRtc(meshtastic_Position p, bool isLocal, bool forceUpdate) {
    RTCQuality quality = isLocal ? RTCQualityNTP : RTCQualityFromNet;

    // Use RV8803 with threshold checking
    RV8803SyncResult result = syncRV8803Time(quality, p.time, forceUpdate);

    if (result == RV8803SyncResult::RTC_NOT_AVAILABLE) {
        // Fall back to existing RTC handling
        struct timeval tv = { .tv_sec = p.time, .tv_usec = 0 };
        perhapsSetRTC(quality, &tv, forceUpdate);
    }
}
```

## API Reference

### Initialization

| Function | Description |
|----------|-------------|
| `begin(TwoWire&, uint8_t)` | Initialize I2C communication |
| `isInitialized()` | Check if device is ready |
| `checkVoltage()` | Verify backup voltage is sufficient |

### Time/Date Operations

| Function | Description |
|----------|-------------|
| `getDateTime(RV8803DateTime&)` | Read current date/time |
| `setDateTime(const RV8803DateTime&)` | Set date/time |
| `getEpoch(uint32_t&)` | Get Unix timestamp |
| `setEpoch(uint32_t)` | Set from Unix timestamp |
| `updateIfDelta(uint32_t, uint32_t&)` | **Conditional update with threshold** |
| `resetHundredths()` | Zero hundredths for sync |

### Alarm Operations

| Function | Description |
|----------|-------------|
| `setAlarm(const RV8803Alarm&)` | Configure alarm |
| `getAlarm(RV8803Alarm&)` | Read alarm config |
| `enableAlarmInterrupt(bool)` | Enable/disable INT output |
| `isAlarmTriggered(bool&)` | Check alarm flag |
| `clearAlarmFlag()` | Clear alarm flag |

### Timer Operations

| Function | Description |
|----------|-------------|
| `setTimer(const RV8803Timer&)` | Configure countdown timer |
| `getTimer(RV8803Timer&)` | Read timer config |
| `enableTimer(bool)` | Start/stop timer |
| `enableTimerInterrupt(bool)` | Enable/disable INT output |
| `isTimerExpired(bool&)` | Check timer flag |
| `clearTimerFlag()` | Clear timer flag |

### Event/Timestamp Operations

| Function | Description |
|----------|-------------|
| `configureEventInput(...)` | Configure EVI pin |
| `getTimestamp(RV8803Timestamp&)` | Read captured timestamp |
| `isEventOccurred(bool&)` | Check event flag |
| `clearEventFlag()` | Clear and reset capture |

### Calibration

| Function | Description |
|----------|-------------|
| `setCalibrationOffset(int8_t)` | Set offset (-64 to +63) |
| `getCalibrationOffset(int8_t&)` | Read current offset |

### Clock Output

| Function | Description |
|----------|-------------|
| `setClockOutput(RV8803ClockOut)` | Set frequency |
| `enableClockOutput(bool)` | Enable/disable CLKOUT |

## Error Handling

All functions return `RV8803Error`:

```cpp
enum class RV8803Error : uint8_t {
    OK = 0,                    // Success
    I2C_ERROR = 1,             // I2C communication failed
    INVALID_PARAM = 2,         // Invalid parameter
    NOT_INITIALIZED = 3,       // Device not initialized
    TIME_INVALID = 4,          // Time value out of range
    VOLTAGE_LOW = 5,           // Backup voltage too low
    WRITE_VERIFY_FAILED = 6,   // Write verification failed
    THRESHOLD_NOT_MET = 7,     // Delta below update threshold
    DEVICE_NOT_FOUND = 8       // Device not responding
};
```

## Configuration

### Update Threshold

The default threshold is 5 minutes (300 seconds). Adjust as needed:

```cpp
// Change threshold at runtime
rtc.setUpdateThreshold(120);  // 2 minutes

// Or during construction
RV8803 rtc(600);  // 10 minutes
```

### I2C Address

Default address is 0x32. The RV-8803-C7 does not support address configuration.

## Hardware Notes

- **I2C Speed**: Supports up to 400kHz
- **Voltage Range**: 1.5V to 5.5V
- **Backup Current**: 240nA typical at 3V
- **Accuracy**: ±3.0 ppm over -40°C to +85°C
- **Pull-ups**: SDA/SCL require external pull-up resistors

## References

- [RV-8803-C7 Datasheet](https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-8803-C7.pdf)
- [RV-8803-C7 Application Manual](https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-8803-C7_App-Manual.pdf)
- [SparkFun RV-8803 Arduino Library](https://github.com/sparkfun/SparkFun_RV-8803_Arduino_Library)
- [NASA JPL Power of 10 Rules](https://en.wikipedia.org/wiki/The_Power_of_10:_Rules_for_Developing_Safety-Critical_Code)
