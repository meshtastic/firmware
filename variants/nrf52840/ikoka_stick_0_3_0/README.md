# IKOKA STICK 0.3.0 - Meshtastic Firmware Variant

Meshtastic firmware variant for the **IKOKA STICK 0.3.0** board with **E22-400M33S (SX1268)** LoRa module.

## Hardware Overview

| Component | Specification |
|-----------|--------------|
| **MCU** | Seeed Studio XIAO nRF52840 |
| **LoRa Module** | EBYTE E22-400M33S (SX1268) |
| **Frequency** | 410-493 MHz (433/470 MHz bands) |
| **Max TX Power** | 33 dBm (2W) with internal PA |
| **TCXO** | 32 MHz @ 2.2V |
| **Display** | SSD1306 OLED (128x64, I2C) |
| **User Button** | Single button on GPIO |

## Features

- ✅ Full SX1268 chip support (not SX1262)
- ✅ 433/470 MHz operation
- ✅ Up to 33 dBm (2W) output power
- ✅ Automatic PA gain compensation
- ✅ TCXO support (2.2V reference)
- ✅ RF switch control via DIO2 + RXEN
- ✅ SSD1306 OLED display
- ✅ User button integration
- ✅ Battery voltage monitoring

## Why This Variant Exists

The original `seeed_xiao_nrf52840_kit` firmware is configured for the **SX1262** chip (used in Wio-SX1262 modules), but the IKOKA STICK 0.3.0 uses the **E22-400M33S** module which is based on the **SX1268** chip.

### Key Differences:

| Aspect | SX1262 (Wio Module) | SX1268 (E22-400M33S) |
|--------|---------------------|----------------------|
| **Chip Type** | SX1262 | SX1268 |
| **Frequency Range** | 150-960 MHz (typ. 868/915 MHz) | 410-810 MHz (typ. 433/470 MHz) |
| **Max Chip Output** | 22 dBm | 22 dBm |
| **Module Max Output** | 22 dBm (no PA) | **33 dBm (with PA)** |
| **TCXO Voltage** | 1.8V | **2.2V** |
| **RF Switch** | DIO2 only | **DIO2 + RXEN** |

Without this variant, the radio would fail to initialize with error `-707` (chip mismatch).

## Pin Mapping

| Function | XIAO Pin | nRF52840 GPIO | E22 Module Pin | Notes |
|----------|----------|---------------|----------------|-------|
| SPI CS | D4 | P0.04 | NSS | Chip select |
| DIO1 | D1 | P0.03 | DIO1 | Interrupt |
| BUSY | D3 | P0.29 | BUSY | Status |
| RESET | D2 | P0.28 | RST | Reset |
| RXEN | D5 | P0.05 | RXEN | RF switch RX enable |
| SPI SCK | D8 | P1.13 | SCK | SPI clock |
| SPI MISO | D9 | P1.14 | MISO | SPI data in |
| SPI MOSI | D10 | P1.15 | MOSI | SPI data out |
| Button | D0 | P0.02 | - | User button |
| Display SDA | D6 | P1.11 | - | I2C data (SSD1306) |
| Display SCL | D7 | P1.12 | - | I2C clock (SSD1306) |

**Note:** TXEN is controlled internally by DIO2 (no external pin needed).

## Power Configuration

The E22-400M33S features an **internal power amplifier (PA)** with **12 dB gain** at full power:

```
SX1268 Chip (21 dBm) → PA (+12 dB) → RF Output (33 dBm / 2W)
```

### Power Scaling

The firmware uses **virtual gain compensation** to map the Meshtastic app's 0-30 dBm range to the module's full 0-33 dBm capability:

| User Setting (App) | SX1268 Output | PA Output | Notes |
|-------------------|---------------|-----------|-------|
| 30 dBm | 21 dBm | **33 dBm** | Maximum power |
| 25 dBm | 16 dBm | 28 dBm | |
| 20 dBm | 11 dBm | 23 dBm | |
| 15 dBm | 6 dBm | 18 dBm | |
| 10 dBm | 1 dBm | 13 dBm | |

- **`TX_GAIN_LORA = 9`**: Virtual gain for power scaling
- **`SX126X_MAX_POWER = 21`**: Maximum SX1268 input to PA

See [`POWER_CONFIGURATION.md`](POWER_CONFIGURATION.md) for detailed technical information.

## Building the Firmware

### Prerequisites

- PlatformIO (CLI or IDE extension for VS Code)
- Python 3
- Git

### Build Commands

```bash
# Clone Meshtastic firmware repository
git clone https://github.com/meshtastic/firmware.git
cd firmware

# Copy this variant to the firmware tree
# (if not already present in official repo)
cp -r /path/to/ikoka_stick_0_3_0 variants/nrf52840/

# Build
pio run -e ikoka_stick_0_3_0

# Build and upload
pio run -e ikoka_stick_0_3_0 -t upload
```

The compiled UF2 file will be located at:
```
.pio/build/ikoka_stick_0_3_0/firmware-ikoka_stick_0_3_0-<version>.uf2
```

See [`BUILD_INSTRUCTIONS.md`](BUILD_INSTRUCTIONS.md) for detailed build instructions.

## Flashing

### Method 1: UF2 Bootloader (Recommended)

1. Double-press the RESET button on XIAO nRF52840
2. Board appears as USB drive "XIAO-SENSE"
3. Drag and drop the `.uf2` file onto the drive
4. Board automatically flashes and reboots

### Method 2: PlatformIO Upload

```bash
pio run -e ikoka_stick_0_3_0 -t upload
```

## Configuration

### Regional Settings

Compatible with 433 MHz regions:
- `EU_433` (Europe 433 MHz ISM band)
- `MY_433` (Malaysia)
- `PH_433` (Philippines)  
- `ANZ_433` (Australia/New Zealand)
- `KZ_433` (Kazakhstan)
- `UA_433` (Ukraine)

Or use `US` region with frequency override for 432-434 MHz.

### Power Settings

**⚠️ Important:**
- Maximum legal power varies by region (typically 10-20 dBm ERP)
- 33 dBm (2W) may require amateur radio license
- Enable "Licensed Amateur Operator" mode in app to access full power
- Always check local regulations

### Licensed Mode

To bypass regional power limits (if legally permitted):

1. Open Meshtastic app
2. Go to **Settings → User**
3. Enable **"Licensed Amateur Operator"**
4. Enter your call sign (optional)

This allows the full 0-33 dBm power range.

## Technical Notes

### TCXO Configuration

The E22-400M33S uses a **32 MHz TCXO** powered by DIO3 at **2.2V**:

```c
#define SX126X_DIO3_TCXO_VOLTAGE 2.2
#define TCXO_OPTIONAL
```

### RF Switch

The module uses a **hybrid RF switch configuration**:
- **DIO2** controls TXEN internally (automatic via firmware)
- **RXEN** controlled externally via GPIO (P0.05)

```c
#define SX126X_RXEN D5
#define SX126X_TXEN RADIOLIB_NC  // DIO2 controls internally
#define SX126X_DIO2_AS_RF_SWITCH
```

### Current Limit

SX1268 current limit set to **140 mA** (chip default):

```c
float currentLimit = 140;  // SX126xInterface.h
```

## Power Supply Requirements

| Operating Mode | Current | Notes |
|---------------|---------|-------|
| **Idle** | ~20 mA | Sleep mode |
| **RX** | ~30 mA | Receiving |
| **TX @ 20 dBm** | ~200 mA | Low power TX |
| **TX @ 30 dBm** | ~500 mA | High power TX |
| **TX @ 33 dBm** | ~600 mA | **Maximum power** |

**Recommendations:**
- USB 3.0 port (up to 900 mA) for full power operation
- External 3.3-5V power supply (1A+) recommended for 33 dBm
- USB 2.0 (500 mA limit) may cause brownout at maximum power
- Add 100-470 µF capacitor near module for power stability

## Safety Guidelines

### ⚠️ CRITICAL WARNINGS

1. **NEVER transmit without antenna**
   - High power without proper load can damage the PA
   - Use antenna rated for 433 MHz (not 868/915 MHz)
   - Check SWR < 2:1

2. **Thermal management**
   - Module can reach 50-70°C at 33 dBm
   - Ensure adequate ventilation
   - Avoid continuous TX at maximum power

3. **Regulatory compliance**
   - Check local regulations before operating
   - Most regions restrict 433 MHz to 10-20 dBm ERP
   - Amateur radio license may be required for 33 dBm
   - Illegal operation can result in fines

4. **Power supply**
   - Use adequate power source (see table above)
   - Voltage drop can cause brownout/reset
   - Add bulk capacitors for stability

## Troubleshooting

### Radio Fails to Initialize

**Symptom:** `SX126x init result -707`

**Causes:**
- Wrong chip type (SX1262 config used instead of SX1268)
- Missing `USE_SX1268` define
- BUSY pin not connected or wrong

**Solution:**
- Verify this variant is being used (not `seeed_xiao_nrf52840_kit`)
- Check serial output for `USE_SX1268` mention
- Verify hardware connections

### Low Power Output

**Symptom:** Output power much lower than expected

**Possible causes:**
- Regional power limits active (not in licensed mode)
- Insufficient power supply (voltage sag)
- Poor antenna or high SWR
- PA not functioning (hardware issue)

**Solutions:**
- Enable "Licensed Amateur Operator" mode
- Use better power supply (USB 3.0 or external)
- Check antenna connection and SWR
- Test with known-good dummy load

### Device Resets During TX

**Symptom:** Brownout/reset when transmitting

**Cause:** Power supply can't handle current spike

**Solution:**
- Use external 5V supply (1A+)
- Add 100-470 µF capacitor near power input
- Reduce TX power setting
- Switch to USB 3.0 port

## Serial Output Example

Successful initialization should show:

```
INFO | S:B:255,2.7.17.b5e952b00,ikoka_stick_0_3_0,meshtastic/firmware
DEBUG | SX126xInterface(cs=4, irq=1, rst=2, busy=3)
DEBUG | SX126X_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at 2.200000 V
INFO | Start meshradio init
INFO | Set radio: region=US, power=30
INFO | Final Tx power: 21 dBm
DEBUG | Current limit set to 140.000000
DEBUG | Set DIO2 as RF switch, result: 0
DEBUG | Use MCU pin 5 as RXEN and pin -1 as TXEN to control RF switching
INFO | Set RX gain to boosted mode; result: 0
INFO | SX1268 init success, TCXO, Vref 2.200000V
```

## Known Issues

- Some E22-400M33S modules may have defective or non-functional PAs
  - Verify module is genuine EBYTE product
  - Test power output with calibrated meter
  - Module should warm up during high-power TX

## References

- [IKOKA STICK Documentation](https://ndoo.sg/projects:amateur_radio:meshtastic:diy_devices:ikoka_stick)
- [E22-400M33S Product Page](https://www.cdebyte.com/products/E22-400M33S)
- [E22-M Series User Manual](https://www.ebyte.com/en/pdf-down.aspx?id=1933)
- [SX1268 Datasheet](https://www.semtech.com/products/wireless-rf/lora-connect/sx1268)
- [Meshtastic Firmware Repository](https://github.com/meshtastic/firmware)
- [Seeed XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/)

## Version Information

- **Firmware Base:** Meshtastic 2.7.17 (commit b5e952b00)
- **Build Date:** 2026-01-18
- **Variant Version:** 1.0

## Contributing

This variant was developed through community collaboration. Contributions, bug reports, and improvements are welcome!

## License

This variant follows the Meshtastic firmware license (GPL v3).

## Acknowledgments

- IKOKA STICK hardware design by [ndoo](https://ndoo.sg/)
- Meshtastic firmware by the [Meshtastic project](https://meshtastic.org/)
- Testing and development support from the community

---

**Document Status:** Production Ready  
**Last Updated:** 2026-01-18  
**Maintainer:** Community
