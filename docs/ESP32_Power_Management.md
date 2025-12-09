# ESP32 Power Management - Lessons Learned

## Overview

This document summarizes lessons learned from attempting to implement advanced power management on ESP32 for Meshtastic, and proposes a simpler approach using manual CPU frequency control.

## What We Tried (And Why It Failed)

### ESP-IDF Dynamic Frequency Scaling (DFS)

We attempted to use ESP-IDF's power management framework with:

- `CONFIG_PM_ENABLE=y`
- `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`
- Custom Arduino-ESP32 build (m1nl's fork)
- PM locks to control when DFS/sleep is allowed

### Why DFS Doesn't Work Well for Meshtastic

#### 1. FreeRTOS Holds CPU at Maximum

ESP-IDF's FreeRTOS port acquires `CPU_FREQ_MAX` locks whenever ANY task is ready to run:

```
Lock stats:
Name            Type            Arg    Active
rtos1           CPU_FREQ_MAX    0      1       ← Always held when tasks exist!
rtos0           CPU_FREQ_MAX    0      0
```

DFS only reduces frequency when the system is **truly idle** (running idle task on both cores). A Meshtastic device with constant radio activity is never truly idle.

#### 2. Bluetooth Holds APB at Maximum

When BLE is connected, NimBLE holds `APB_FREQ_MAX` lock for timing accuracy:

```
bt              APB_FREQ_MAX    0      1       ← Held while BLE connected
```

This keeps CPU at max whenever a phone is connected.

#### 3. Automatic Light Sleep Breaks Peripherals

When `light_sleep_enable = true` and PM lock is released:

- USB-CDC serial disconnects immediately
- SPI transactions get interrupted (display corruption)
- Requires explicit wakeup source configuration

#### 4. Requires Special Build Environment

ESP-IDF PM requires a custom Arduino-ESP32 build, adding complexity and maintenance burden.

## The Simple Solution: Manual CPU Frequency Control

### Key Insight

**We don't need DFS or PM locks.** Arduino-ESP32 provides `setCpuFrequencyMhz()` which directly sets CPU speed, bypassing all PM lock logic. Tasks continue running at the lower frequency - just slower.

### Implementation Strategy

```
┌─────────────────────────────────────────────────────────────┐
│                    CPU Frequency Control                     │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   ACTIVE (Work to do)                                        │
│   ┌────────────────────────────────────────────┐            │
│   │  CPU: 240 MHz                              │            │
│   │  Triggers:                                 │            │
│   │  - Screen turns on                         │            │
│   │  - User input                              │            │
│   │  - Radio TX                                │            │
│   │  - BLE config/sync                         │            │
│   │  - WiFi active                             │            │
│   └────────────────────────────────────────────┘            │
│                         │                                    │
│                         ▼                                    │
│   IDLE (Background only)                                     │
│   ┌────────────────────────────────────────────┐            │
│   │  CPU: 10-80 MHz                            │            │
│   │  Triggers:                                 │            │
│   │  - Screen timeout                          │            │
│   │  - No pending work                         │            │
│   │  - BLE config complete                     │            │
│   └────────────────────────────────────────────┘            │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Minimum Safe Frequencies

| Chip     | Min Freq | Notes                       |
| -------- | -------- | --------------------------- |
| ESP32    | 10 MHz   | With UART on REF_TICK clock |
| ESP32-S2 | 10 MHz   | With UART on REF_TICK clock |
| ESP32-S3 | 40 MHz   | USB-CDC needs higher clock  |
| ESP32-C3 | 10 MHz   | Limited testing             |
| ESP32-C6 | 10 MHz   | Limited testing             |

**Note:** At 10 MHz on ESP32-S3, USB serial will disconnect. Use 40+ MHz if debugging needed.

## Proposed Implementation

### Simple API

```cpp
// power.h

/// Set CPU to maximum frequency for active work
void setCpuFullSpeed();

/// Set CPU to low-power frequency for idle/background
void setCpuLowPower();

/// Request full speed for a duration (auto-expires)
void requestFullSpeed(uint32_t durationMs);

/// Force full speed until cleared (for BLE config, etc.)
void setForceFullSpeed(bool force);

/// Check if we should stay at full speed
bool needsFullSpeed();
```

### Implementation

```cpp
// power.cpp

static bool forceFullSpeed = false;
static uint32_t fullSpeedUntil = 0;

void setCpuFullSpeed() {
    setCpuFrequencyMhz(240);
}

void setCpuLowPower() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    setCpuFrequencyMhz(40);  // USB-CDC minimum
#else
    setCpuFrequencyMhz(10);  // Maximum power savings
#endif
}

void requestFullSpeed(uint32_t durationMs) {
    uint32_t until = millis() + durationMs;
    if (until > fullSpeedUntil) {
        fullSpeedUntil = until;
    }
    setCpuFullSpeed();
}

void setForceFullSpeed(bool force) {
    forceFullSpeed = force;
    if (force) {
        setCpuFullSpeed();
    }
}

bool needsFullSpeed() {
    if (forceFullSpeed) return true;
    if (millis() < fullSpeedUntil) return true;
    return false;
}
```

### Integration Points

#### 1. Screen State

```cpp
void onScreenOn() {
    setCpuFullSpeed();
}

void onScreenOff() {
    if (!needsFullSpeed()) {
        setCpuLowPower();
    }
}
```

#### 2. BLE Config Sync

```cpp
void onConfigStart() {
    setForceFullSpeed(true);  // Fast sync
}

void onConfigComplete() {
    setForceFullSpeed(false); // Allow low power
}
```

#### 3. Radio TX

```cpp
void startTransmit() {
    requestFullSpeed(500);  // Full speed during TX
}
```

#### 4. User Input

```cpp
void onUserInput() {
    requestFullSpeed(5000);  // 5 seconds of responsiveness
}
```

#### 5. Main Loop Check

```cpp
void loop() {
    // ... existing code ...

    // End of loop - reduce power if idle
    if (!screenOn && !needsFullSpeed()) {
        static bool isLowPower = false;
        if (!isLowPower) {
            setCpuLowPower();
            isLowPower = true;
        }
    }
}
```

## Power Consumption Estimates

| State             | CPU Freq | Est. Current | Notes          |
| ----------------- | -------- | ------------ | -------------- |
| Active + Radio TX | 240 MHz  | 80-120 mA    | Peak usage     |
| Active + Screen   | 240 MHz  | 50-80 mA     | Normal use     |
| Idle (80 MHz)     | 80 MHz   | 25-40 mA     | Background RX  |
| Idle (10 MHz)     | 10 MHz   | 10-20 mA     | Minimum active |
| Light Sleep       | -        | 0.8-2 mA     | CPU stopped    |
| Deep Sleep        | -        | 10-150 µA    | RAM lost       |

## What This Approach Does

✅ Simple implementation - just `setCpuFrequencyMhz()` calls  
✅ No special build requirements - works with stock Arduino-ESP32  
✅ Predictable behavior - you control exactly when frequency changes  
✅ Significant power savings - 10 MHz uses ~1/24th CPU power of 240 MHz  
✅ All peripherals keep working - just slower

## What This Approach Does NOT Do

❌ True automatic DFS based on load  
❌ Light sleep (CPU stops) - that requires explicit `esp_light_sleep_start()`  
❌ Sub-10MHz operation  
❌ Automatic peripheral-aware scaling

## Comparison: Manual vs DFS

| Aspect            | Manual Control   | ESP-IDF DFS            |
| ----------------- | ---------------- | ---------------------- |
| Build complexity  | None             | Custom Arduino-ESP32   |
| BLE compatibility | Full             | Holds CPU at max       |
| USB-CDC           | Works at 40+ MHz | Breaks with auto-sleep |
| Predictability    | Full control     | Opaque lock system     |
| Power savings     | Good (manual)    | Limited (locks)        |
| Implementation    | Simple           | Complex                |

## Future Enhancements

1. **Activity monitoring** - Track actual CPU usage to make smarter decisions
2. **Configurable frequencies** - User settings for min/max frequencies
3. **Per-peripheral awareness** - Don't reduce during SPI transactions
4. **Metrics tracking** - Log time spent at each frequency level
5. **Light sleep integration** - Explicit light sleep when truly idle

## References

- [ESP-IDF Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html)
- [Arduino-ESP32 CPU Frequency API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html)
- [ESP32 Datasheet - Power Consumption](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
