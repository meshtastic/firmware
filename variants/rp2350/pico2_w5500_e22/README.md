# Raspberry Pi Pico 2 + W5500 + E22-900M30S — Meshtastic Variant

Meshtastic support for a **Raspberry Pi Pico 2** (RP2350, 4 MB flash) with an external **W5500** Ethernet module and an **EBYTE E22-900M30S** LoRa module.

---

## Required Hardware

| Component | Model                         | Notes                                    |
|-----------|-------------------------------|------------------------------------------|
| MCU       | Raspberry Pi Pico 2           | RP2350 @ 150 MHz, 512 KB RAM, 4 MB flash |
| Ethernet  | W5500 module                  | Any WIZnet W5500 breakout board          |
| LoRa      | EBYTE E22-900M30S             | SX1262 + 30 dBm PA, 868/915 MHz         |

---

## Pinout

### System pins (Pico 2, fixed)

| GPIO | Function                                      |
|------|-----------------------------------------------|
| GP24 | VBUS sense — HIGH when USB is connected       |
| GP25 | User LED (heartbeat)                          |
| GP29 | ADC3 — VSYS/3, measures supply voltage        |

### W5500 Ethernet (SPI0)

| W5500 signal | Pico 2 GPIO |
|--------------|-------------|
| MISO         | GP16        |
| CS / SCS     | GP17        |
| SCK          | GP18        |
| MOSI         | GP19        |
| RST          | GP20        |
| INT          | — (nc)      |
| VCC          | 3.3V        |
| GND          | GND         |

> SPI0 is reserved for the W5500.

### E22-900M30S LoRa (SPI1)

| E22 signal | Pico 2 GPIO | Notes                                          |
|------------|-------------|------------------------------------------------|
| SCK        | GP10        | SPI1 clock                                     |
| MOSI       | GP11        | SPI1 TX                                        |
| MISO       | GP12        | SPI1 RX                                        |
| NSS / CS   | GP13        | Chip select                                    |
| RESET      | GP15        | Active LOW reset                               |
| DIO1       | GP14        | IRQ interrupt                                  |
| BUSY       | GP2         | Module busy indicator                          |
| RXEN       | GP3         | LNA enable — held HIGH permanently            |
| TXEN       | ← DIO2      | See wiring note below                          |
| VCC        | 3.3V        | Add a 100 µF capacitor close to the module    |
| GND        | GND         | —                                              |

> See `wiring.svg` in this directory for the full connection diagram.

---

## Special wiring: DIO2 → TXEN bridge on the E22 module

The E22-900M30S does **not** connect DIO2 to the TXEN pin of its PA internally. They must be bridged with a short wire or solder bridge **on the module itself**:

```
E22 DIO2 pin  ──┐
                ├── wire / solder bridge on the module
E22 TXEN pin  ──┘
```

With this bridge in place, `SX126X_DIO2_AS_RF_SWITCH` causes the SX1262 to drive DIO2 HIGH automatically during TX, enabling the PA without needing an RP2350 GPIO for TXEN.

**Without this bridge the module will not transmit.**

---

## Build

```bash
pio run -e pico2_w5500_e22
```

### Flash — BOOTSEL mode

1. Hold the **BOOTSEL** button on the Pico 2.
2. Connect USB to the PC — it appears as a `RPI-RP2` storage drive.
3. Copy the `.uf2` file:

```
.pio/build/pico2_w5500_e22/firmware-pico2_w5500_e22-*.uf2
```

Or directly with picotool:

```bash
pio run -e pico2_w5500_e22 -t upload
```

---

## Network usage

This board uses Ethernet (no Wi-Fi). From the Meshtastic app:

- **Enable Ethernet** under `Config → Network → Ethernet Enabled`
- **DHCP** by default; static IP can also be configured

Services available once connected:

| Service | Details                     |
|---------|-----------------------------|
| NTP     | Time synchronization        |
| MQTT    | Messages to external broker |
| API     | TCP socket on port 4403     |
| Syslog  | Remote logging (optional)   |

---

## Technical notes

### LoRa — RF control

| Define                         | Effect                                                        |
|--------------------------------|---------------------------------------------------------------|
| `SX126X_ANT_SW 3`              | GP3 (RXEN) driven HIGH at init and never toggled again        |
| `SX126X_DIO2_AS_RF_SWITCH`     | SX1262 drives DIO2 HIGH during TX → enables TXEN via bridge  |
| `SX126X_DIO3_TCXO_VOLTAGE 1.8` | E22 TCXO controlled by DIO3                                  |
| `-D EBYTE_E22_900M30S`         | Sets `TX_GAIN_LORA=7`, max power 22 dBm                      |

> RXEN and TXEN may both be HIGH simultaneously during TX — this is safe for the E22 RF switch.

### Ethernet

- Library: `arduino-libraries/Ethernet@^2.0.2` (supports W5100/W5200/W5500 auto-detection).
- SPI0 is explicitly initialized with pins GP16/18/19 before `Ethernet.init()`.
- DHCP timeout is set to 10 s (instead of the default 60 s) to avoid blocking LoRa startup.

### HW_VENDOR

Mapped to `meshtastic_HardwareModel_PRIVATE_HW` — no dedicated model exists in the Meshtastic protobuf for this hardware combination yet.

---

## Memory usage (reference build)

| Resource | Used    | Total    | %     |
|----------|---------|----------|-------|
| RAM      | 94 KB   | 512 KB   | 18%   |
| Flash    | 964 KB  | 3.58 MB  | 26.3% |
