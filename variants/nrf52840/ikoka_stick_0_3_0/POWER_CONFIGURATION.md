# E22-400M33S Power Configuration - Technical Reference

## Overview

The E22-400M33S module integrates a power amplifier (PA) that amplifies the SX1268 chip's output to achieve up to **33 dBm (2W)** at 433/470 MHz. This document explains the technical details of the power configuration.

## Power Amplifier Specifications

### From E22-M Series User Manual

Based on the RF output power comparison curve:

| Parameter | Value | Source |
|-----------|-------|--------|
| **Maximum Output Power** | 33 dBm (2W) | Module specification |
| **SX1268 Input for 33 dBm** | 21 dBm | RF power curve |
| **Actual PA Gain** | 12 dB | Measured from curve (33 - 21 = 12) |
| **Supply Voltage** | 2.3-5.5V | Module specification |
| **Supply Current @ 33 dBm** | ~600 mA | Estimated |

### Power Chain

```
User Sets    Firmware       SX1268         PA           RF Output
30 dBm   →   Scales   →   21 dBm    →   +12 dB   →    33 dBm
            (30-9=21)                    (actual)      (2 watts)
```

## Firmware Configuration Strategy

### The Challenge

**Problem:** Meshtastic app limits TX power setting to **0-30 dBm**, but the module can output **33 dBm**.

**Solution:** Use **virtual gain** instead of actual PA gain for power scaling.

### Configuration Values

In `firmware/src/configuration.h`:

```c
#ifdef EBYTE_E22_400M33S
// E22-400M33S: 433/470MHz variant - per RF output power curve from E22-M Series manual
// Actual PA gain: 12 dB (21 dBm input → 33 dBm output from curve)
// Virtual gain: 9 dB - scales app's 0-30 dBm range to module's 0-33 dBm range
// This makes: App 30dBm = SX1268 21dBm = Module 33dBm (full power)
#define TX_GAIN_LORA 9
#define SX126X_MAX_POWER 21
#endif
```

### Why Virtual Gain?

| Metric | Actual PA Gain (12 dB) | Virtual Gain (9 dB) |
|--------|------------------------|---------------------|
| **User sets 30 dBm** | 30 - 12 = 18 dBm to chip | 30 - 9 = **21 dBm** to chip ✅ |
| **Final output** | 18 + 12 = **30 dBm** ❌ | 21 + 12 = **33 dBm** ✅ |
| **Max chip power** | Underutilized | Fully utilized ✅ |
| **App max (30 dBm)** | → Module 30 dBm | → Module **33 dBm** ✅ |

**Virtual gain of 9 dB allows users to access the full 33 dBm output by setting 30 dBm in the app.**

## Power Scaling Table

### User Setting vs Actual Output

| App Setting | TX_GAIN Subtraction | SX1268 Output | Actual PA Gain | Module Output |
|-------------|---------------------|---------------|----------------|---------------|
| 30 dBm | 30 - 9 = 21 dBm | **21 dBm** | +12 dB | **33 dBm** ✅ |
| 28 dBm | 28 - 9 = 19 dBm | 19 dBm | +12 dB | 31 dBm |
| 25 dBm | 25 - 9 = 16 dBm | 16 dBm | +12 dB | 28 dBm |
| 20 dBm | 20 - 9 = 11 dBm | 11 dBm | +12 dB | 23 dBm |
| 15 dBm | 15 - 9 = 6 dBm | 6 dBm | +12 dB | 18 dBm |
| 10 dBm | 10 - 9 = 1 dBm | 1 dBm | +12 dB | 13 dBm |
| 0 dBm | 0 - 9 = -9 dBm | -9 dBm | +12 dB | 3 dBm |

### Key Points

- ✅ **User setting 30 dBm** maps to **module output 33 dBm** (full power)
- ✅ **Linear scaling** across the range
- ✅ **SX1268 never exceeds 21 dBm** (safe for PA input)
- ✅ **App's 0-30 dBm range** maps to **module's 0-33 dBm capability**

## Firmware Implementation

### Power Limiting Code

From `firmware/src/mesh/RadioInterface.cpp`:

```c
void RadioInterface::limitPower(int8_t loraMaxPower)
{
    uint8_t maxPower = 255; // No limit
    
    // Apply regional power limits (if not licensed)
    if (myRegion->powerLimit)
        maxPower = myRegion->powerLimit;
        
    if ((power > maxPower) && !devicestate.owner.is_licensed) {
        LOG_INFO("Lower transmit power because of regulatory limits");
        power = maxPower;
    }
    
    // Subtract TX_GAIN_LORA (virtual gain)
    if (TX_GAIN_LORA > 0 && !devicestate.owner.is_licensed) {
        LOG_INFO("Requested Tx power: %d dBm; Device LoRa Tx gain: %d dB", power, TX_GAIN_LORA);
        power -= TX_GAIN_LORA;  // power = power - 9
    }
    
    // Clamp to SX126X_MAX_POWER (21 dBm)
    if (power > loraMaxPower)
        power = loraMaxPower;
        
    LOG_INFO("Final Tx power: %d dBm", power);
}
```

### Execution Flow Example

**User sets 30 dBm in app:**

1. `power = 30` (user request)
2. Regional limit check: `if (30 > 30) → false` (US region allows 30 dBm)
3. TX_GAIN subtraction: `power = 30 - 9 = 21 dBm`
4. Max clamp: `if (21 > 21) → false`
5. **Final: SX1268 set to 21 dBm**
6. PA amplifies: 21 + 12 = **33 dBm output**

## Licensed Amateur Operator Mode

### Why It's Important

The power scaling **only works when regional limits don't interfere**.

### Without Licensed Mode

Example: EU_433 region (10 dBm limit)

```
User sets: 30 dBm
  ↓
Regional limit: 30 > 10 → power = 10 dBm
  ↓
TX_GAIN subtract: 10 - 9 = 1 dBm
  ↓
SX1268: 1 dBm
  ↓
PA: 1 + 12 = 13 dBm output ❌ (severely limited)
```

### With Licensed Mode

```
User sets: 30 dBm
  ↓
Regional limit: BYPASSED ✅
  ↓
TX_GAIN subtract: BYPASSED ✅
  ↓  
SX1268: 30 dBm → clamped to 21 dBm
  ↓
PA: 21 + 12 = 33 dBm output ✅ (full power)
```

### Enabling Licensed Mode

In Meshtastic app:
1. **Settings → User**
2. Enable **"Licensed Amateur Operator"** ✅
3. Enter call sign (optional)

This sets `devicestate.owner.is_licensed = true`, which bypasses both:
- Regional power limits
- TX_GAIN_LORA subtraction

## Hardware Considerations

### SX1268 Input Limits

From RF power curve in E22-M Series manual:

| SX1268 Input | Module Output | Notes |
|--------------|---------------|-------|
| -9 dBm | ~3 dBm | Minimum |
| 0 dBm | ~12 dBm | |
| 10 dBm | ~22 dBm | |
| **21 dBm** | **33 dBm** | **Maximum recommended** ✅ |
| 22 dBm | ~34 dBm | **Exceeds spec** ⚠️ |

**Never exceed 21 dBm input to PA** - can cause:
- Distortion
- Spurious emissions
- PA damage
- Reduced efficiency

### Current Consumption

| Module Output | SX1268 Current | PA Current | Total | Notes |
|---------------|----------------|------------|-------|-------|
| 10 dBm | ~50 mA | ~100 mA | ~150 mA | Low power |
| 20 dBm | ~100 mA | ~200 mA | ~300 mA | Medium power |
| 28 dBm | ~120 mA | ~400 mA | ~520 mA | High power |
| **33 dBm** | ~140 mA | ~460 mA | **~600 mA** | **Maximum** |

**Power supply must provide:**
- Stable 3.3-5.5V
- At least 700 mA capacity
- Low impedance (bulk capacitors recommended)

### Thermal Considerations

**At 33 dBm (2W output):**
- Module dissipates ~1W as heat
- Can reach 50-70°C
- Heatsink recommended for continuous operation
- Duty cycle < 50% for passive cooling

## TCXO Configuration

### Settings

```c
#define SX126X_DIO3_TCXO_VOLTAGE 2.2  // DIO3 outputs 2.2V for TCXO
#define TCXO_OPTIONAL                   // Enable TCXO support
```

### Why 2.2V?

From E22-M Series User Manual:
> "DIO3 is used to power a 32MHz TCXO crystal oscillator (the DIO3 is configured to output 2.2V)"

**Incorrect TCXO voltage (e.g., 1.8V) causes:**
- Radio initialization failure (error -707)
- Frequency drift
- Reduced sensitivity

## Serial Output Verification

### Successful Configuration

```
DEBUG | SX126xInterface(cs=4, irq=1, rst=2, busy=3)
DEBUG | SX126X_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at 2.200000 V
INFO  | Start meshradio init
INFO  | Set radio: region=US, power=30
INFO  | Final Tx power: 21 dBm
DEBUG | Current limit set to 140.000000
DEBUG | Set DIO2 as RF switch, result: 0
INFO  | SX126x init success, TCXO, Vref 2.200000V
```

**Key indicators:**
- ✅ TCXO: 2.2V
- ✅ Final Tx power: 21 dBm (for 30 dBm request)
- ✅ DIO2 as RF switch
- ✅ Init result: 0 (success)

### With Licensed Mode OFF

```
INFO  | Requested Tx power: 30 dBm; Device LoRa Tx gain: 9 dB
INFO  | Final Tx power: 21 dBm
```

Shows TX_GAIN_LORA subtraction: 30 - 9 = 21 dBm

### With Licensed Mode ON

```
INFO  | Final Tx power: 21 dBm
```

No TX_GAIN message - subtraction bypassed, power directly clamped to max.

## Testing and Validation

### Recommended Test Setup

1. **RF power meter** or **spectrum analyzer**
2. **50Ω dummy load** rated for 2W+
3. **Calibration**: ±0.5 dB accuracy
4. **Frequency**: 432-434 MHz

### Test Procedure

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Set 10 dBm in app | Measure ~13 dBm |
| 2 | Set 15 dBm in app | Measure ~18 dBm |
| 3 | Set 20 dBm in app | Measure ~23 dBm |
| 4 | Set 25 dBm in app | Measure ~28 dBm |
| 5 | Set 30 dBm in app | Measure ~33 dBm |

**Tolerance:** ±1-2 dB is normal due to:
- Measurement accuracy
- PA efficiency variation
- Supply voltage
- Temperature

### Troubleshooting Low Power

If measured output is much lower than expected:

**Check:**
1. ✅ Licensed mode enabled
2. ✅ VCC stable during TX (no sag below 3V)
3. ✅ Antenna/dummy load properly connected
4. ✅ Module is genuine EBYTE E22-400M33S
5. ✅ Module warms up during TX (PA working)
6. ✅ Serial logs show "Final Tx power: 21 dBm"

## Summary

### Configuration Values

```c
#define TX_GAIN_LORA 9              // Virtual gain (not actual 12 dB)
#define SX126X_MAX_POWER 21         // Maximum SX1268 input
#define SX126X_DIO3_TCXO_VOLTAGE 2.2  // TCXO voltage
```

### Power Calculation

```
Module Output = SX1268 Output + Actual PA Gain
Module Output = (User Setting - Virtual Gain) + 12 dB
Module Output = (30 - 9) + 12 = 33 dBm
```

### Key Takeaways

- ✅ Virtual gain (9 dB) allows full 33 dBm from 30 dBm app setting
- ✅ Actual PA gain is 12 dB (from RF power curve)
- ✅ SX1268 limited to 21 dBm input (safe for PA)
- ✅ Licensed mode required for full power operation
- ✅ TCXO must be 2.2V (not 1.8V)

---

**Document Status:** Technical Reference  
**Last Updated:** 2026-01-18  
**Firmware Version:** 2.7.17  
**Based On:** E22-M Series User Manual RF power curve
