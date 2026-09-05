# Pull Request: Add IKOKA STICK 0.3.0 Variant (SX1268/E22-400M33S)

## Summary

This PR adds support for the **IKOKA STICK 0.3.0** board featuring the **EBYTE E22-400M33S (SX1268)** LoRa module operating at 433/470 MHz.

## Hardware Overview

- **Board:** IKOKA STICK 0.3.0 (Seeed XIAO nRF52840 based)
- **LoRa Module:** EBYTE E22-400M33S  
- **Chip:** Semtech SX1268 (**not** SX1262)
- **Frequency:** 410-493 MHz (433/470 MHz ISM bands)
- **Max Power:** 33 dBm (2W) with internal PA
- **Display:** SSD1306 OLED (128x64, I2C)
- **Features:** User button, battery monitoring, TCXO

## Why This Variant Is Needed

The existing `seeed_xiao_nrf52840_kit` variant is configured for **SX1262** chips (Wio-SX1262 modules), but the IKOKA STICK 0.3.0 uses the **E22-400M33S** which is based on the **SX1268** chip.

### Key Differences from Existing Variant

| Aspect | `seeed_xiao_nrf52840_kit` | `ikoka_stick_0_3_0` (new) |
|--------|---------------------------|---------------------------|
| **Radio Chip** | SX1262 | **SX1268** |
| **Frequency** | 868/915 MHz | **433/470 MHz** |
| **Max Module Power** | 22 dBm (no PA) | **33 dBm (with PA)** |
| **TCXO Voltage** | 1.8V | **2.2V** |
| **RF Switch** | DIO2 only | **DIO2 + RXEN** |
| **Display** | None | **SSD1306 OLED** |
| **PA Gain** | 0 dB | **12 dB** |

Without this variant, the radio fails to initialize with error `-707` due to chip mismatch.

## Changes

### New Files

```
firmware/variants/nrf52840/ikoka_stick_0_3_0/
├── variant.h               # Pin definitions and hardware config
├── variant.cpp             # Pin mapping array
├── platformio.ini          # Build environment configuration
├── README.md               # Main documentation
├── BUILD_INSTRUCTIONS.md   # Build and flash guide
└── POWER_CONFIGURATION.md  # Technical power configuration reference
```

### Modified Files

```
firmware/src/configuration.h
  - Added EBYTE_E22_400M33S power configuration block
    #define TX_GAIN_LORA 9
    #define SX126X_MAX_POWER 21
```

## Technical Details

### Chip Configuration

```c
#define USE_SX1268                      // SX1268 chip (not SX1262)
#define SX126X_DIO3_TCXO_VOLTAGE 2.2    // 2.2V TCXO (from E22-M manual)
#define TCXO_OPTIONAL                    // Enable TCXO support
```

### RF Switch Configuration

```c
#define SX126X_RXEN D5                   // P0.05: External RXEN control
#define SX126X_TXEN RADIOLIB_NC          // TXEN controlled by DIO2 internally
#define SX126X_DIO2_AS_RF_SWITCH         // Enable DIO2→TXEN control
```

### Power Amplifier Configuration

The E22-400M33S has a **12 dB PA gain** (21 dBm input → 33 dBm output per RF power curve).

To allow users to access the full 33 dBm capability while staying within the app's 30 dBm limit, we use **virtual gain scaling**:

```c
#define TX_GAIN_LORA 9              // Virtual gain for power scaling
#define SX126X_MAX_POWER 21         // Maximum SX1268 input to PA
```

**Power mapping:**
- User sets 30 dBm → SX1268 outputs 21 dBm → PA amplifies to 33 dBm
- User sets 20 dBm → SX1268 outputs 11 dBm → PA amplifies to 23 dBm
- Linear scaling across entire range

See [`POWER_CONFIGURATION.md`](firmware/variants/nrf52840/ikoka_stick_0_3_0/POWER_CONFIGURATION.md) for detailed explanation.

### Pin Mapping

| Function | XIAO Pin | nRF52840 GPIO | Notes |
|----------|----------|---------------|-------|
| SPI CS | D4 | P0.04 | |
| DIO1 | D1 | P0.03 | Interrupt |
| BUSY | D3 | P0.29 | Status |
| RESET | D2 | P0.28 | |
| RXEN | D5 | P0.05 | RF switch control |
| SPI SCK | D8 | P1.13 | |
| SPI MISO | D9 | P1.14 | |
| SPI MOSI | D10 | P1.15 | |
| Button | D0 | P0.02 | User input |
| Display SDA | D6 | P1.11 | I2C (SSD1306) |
| Display SCL | D7 | P1.12 | I2C (SSD1306) |

## Regional Support

Compatible with 433 MHz regions:
- `EU_433` (Europe)
- `MY_433` (Malaysia)
- `PH_433` (Philippines)
- `ANZ_433` (Australia/New Zealand)
- `KZ_433` (Kazakhstan)
- `UA_433` (Ukraine)

Also works with `US` region + frequency override.

## Testing

### Verified Working

- ✅ Radio initialization (SX1268 detection)
- ✅ TCXO operation at 2.2V
- ✅ RF switch control (DIO2 + RXEN)
- ✅ Power amplifier operation
- ✅ Power scaling (0-33 dBm range)
- ✅ TX/RX at 433 MHz
- ✅ Display (SSD1306 OLED)
- ✅ User button
- ✅ Battery monitoring

### Serial Output (Successful Init)

```
INFO | S:B:255,2.7.17.b5e952b00,ikoka_stick_0_3_0,meshtastic/firmware
DEBUG | SX126xInterface(cs=4, irq=1, rst=2, busy=3)
DEBUG | SX126X_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at 2.200000 V
INFO | Set radio: region=US, power=30
INFO | Final Tx power: 21 dBm
DEBUG | Set DIO2 as RF switch, result: 0
DEBUG | Use MCU pin 5 as RXEN and pin -1 as TXEN to control RF switching
INFO | SX1268 init success, TCXO, Vref 2.200000V
```

## Power Supply Requirements

| Mode | Current |
|------|---------|
| Idle | ~20 mA |
| RX | ~30 mA |
| TX @ 20 dBm | ~200 mA |
| TX @ 30 dBm | ~500 mA |
| **TX @ 33 dBm** | **~600 mA** |

**Note:** USB 3.0 or external power supply recommended for maximum power operation.

## Safety Notes

### ⚠️ Important Warnings

1. **Regulatory Compliance**
   - 33 dBm (2W) may be illegal in most regions without amateur radio license
   - EU_433 typically limited to 10 dBm ERP
   - Users must check local regulations
   - Licensed mode required for full power

2. **Hardware Safety**
   - Never transmit without proper antenna
   - Module can reach 50-70°C at maximum power
   - Adequate power supply required (see table above)

3. **Antenna**
   - Must use 433 MHz antenna (not 868/915 MHz)
   - SWR should be < 2:1

## Documentation

All documentation included in variant directory:
- **README.md** - Complete user guide
- **BUILD_INSTRUCTIONS.md** - Step-by-step build guide
- **POWER_CONFIGURATION.md** - Technical power configuration details

## Build Instructions

```bash
# Build firmware
pio run -e ikoka_stick_0_3_0

# Build and upload
pio run -e ikoka_stick_0_3_0 -t upload
```

Output: `.pio/build/ikoka_stick_0_3_0/firmware-ikoka_stick_0_3_0-<version>.uf2`

## Hardware Availability

- **IKOKA STICK 0.3.0**: [Documentation](https://ndoo.sg/projects:amateur_radio:meshtastic:diy_devices:ikoka_stick)
- **E22-400M33S Module**: [EBYTE Product Page](https://www.cdebyte.com/products/E22-400M33S)
- **Seeed XIAO nRF52840**: [Seeed Studio](https://www.seeedstudio.com/Seeed-XIAO-BLE-nRF52840-p-5201.html)

## References

- [E22-M Series User Manual](https://www.ebyte.com/en/pdf-down.aspx?id=1933)
- [SX1268 Datasheet](https://www.semtech.com/products/wireless-rf/lora-connect/sx1268)
- [IKOKA STICK Project](https://ndoo.sg/projects:amateur_radio:meshtastic:diy_devices:ikoka_stick)

## Compatibility

- **Firmware Version:** 2.7.17 and later
- **Tested On:** 2.7.17 (commit b5e952b00)
- **Architecture:** nRF52840
- **Platform:** Adafruit nRF52

## Checklist

- [x] Code compiles without errors
- [x] Tested on actual hardware
- [x] Documentation complete
- [x] Build instructions provided
- [x] Pin mappings verified
- [x] Radio initialization works
- [x] TX/RX verified
- [x] Power settings correct
- [x] Display functional
- [x] User button functional
- [x] No conflicts with existing variants

## Credits

- **Hardware Design:** IKOKA STICK by [ndoo](https://ndoo.sg/)
- **Variant Development:** Community contribution
- **Testing:** Community volunteers

---

## For Reviewers

### Key Points to Verify

1. **New variant doesn't conflict with existing variants** ✓
2. **Changes to `configuration.h` are scoped** ✓ (only active when `EBYTE_E22_400M33S` defined)
3. **Pin mappings correct for nRF52840** ✓
4. **Power configuration safe** ✓ (SX1268 limited to 21 dBm max)
5. **Documentation comprehensive** ✓

### Testing Recommendations

- Verify compilation: `pio run -e ikoka_stick_0_3_0`
- Check no regressions in other nRF52840 variants
- Review power configuration logic

## Questions?

Feel free to ask for clarifications or request changes. This variant has been thoroughly tested and documented.

---

**Submitted By:** Community  
**Date:** 2026-01-18  
**Firmware Base:** 2.7.17 (b5e952b00)
