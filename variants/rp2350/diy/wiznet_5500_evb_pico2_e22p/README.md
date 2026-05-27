# WIZnet W5500-EVB-Pico2 + E22P-868M30S — Meshtastic Variant

Meshtastic support for the **WIZnet W5500-EVB-Pico2**, a Raspberry Pi Pico 2 development board with an **integrated W5500 Ethernet PHY** on SPI0, paired with an external **EBYTE E22P-868M30S** LoRa module on SPI1.

This variant is hardware-twin of [`pico2_w5500_e22`](../diy/pico2_w5500_e22/) (same LoRa pinout, same W5500 pin mapping) but targets the WIZnet PCB rather than a standalone Pico 2 + external W5500 module. The two key differences are:

|              | `pico2_w5500_e22`                      | `wiznet_5500_evb_pico2_e22p`             |
| ------------ | -------------------------------------- | ---------------------------------------- |
| Board target | `rpipico2` (4 MB flash)                | `wiznet_5500_evb_pico2` (**2 MB flash**) |
| W5500 PHY    | External breakout module (you wire it) | Integrated on the PCB (hard-wired)       |

> ⚠️ Flashing a W5500-EVB-Pico2 with the `pico2_w5500_e22` env builds for a 4 MB flash target and can silently overflow the 2 MB available on the WIZnet PCB. Use this variant for the WIZnet board.

The LoRa wiring and `variant.h` are shared with `diy/pico2_w5500_e22` via `-I variants/rp2350/diy/pico2_w5500_e22` — there is no duplicated pinout file.

---

## Required Hardware

| Component | Model                  | Notes                                                                          |
| --------- | ---------------------- | ------------------------------------------------------------------------------ |
| Board     | WIZnet W5500-EVB-Pico2 | RP2350 @ 150 MHz, 512 KB RAM, **2 MB flash**, on-board W5500                   |
| LoRa      | EBYTE E22P-868M30S     | SX1262 + 30 dBm PA, 868 MHz (Europe band); RFEN combines LNA + PA enable lines |

The W5500-EVB-Pico2 carries a smaller Q-SPI flash (2 MB) than a stock Pi Pico 2 (4 MB). The board target `wiznet_5500_evb_pico2` selects the correct flash size so the linker fails fast if the image overflows, instead of producing a UF2 that gets truncated when flashed to the device.

---

## Pinout

### System pins (W5500-EVB-Pico2, fixed by the PCB)

| GPIO | Function                                |
| ---- | --------------------------------------- |
| GP24 | VBUS sense — HIGH when USB is connected |
| GP25 | User LED (heartbeat)                    |
| GP29 | ADC3 — VSYS/3, measures supply voltage  |

### W5500 Ethernet (SPI0, on-board)

| W5500 signal | RP2350 GPIO |
| ------------ | ----------- |
| MISO         | GP16        |
| CS / SCS     | GP17        |
| SCK          | GP18        |
| MOSI         | GP19        |
| RST          | GP20        |
| INT          | GP21        |
| VCC          | 3.3V        |
| GND          | GND         |

> All Ethernet pins are wired by the PCB — nothing to solder. SPI0 is reserved for the W5500.

### E22P-868M30S LoRa (SPI1, external)

| E22P signal | RP2350 GPIO | Notes                                             |
| ----------- | ----------- | ------------------------------------------------- |
| SCK         | GP10        | SPI1 clock                                        |
| MOSI        | GP11        | SPI1 TX                                           |
| MISO        | GP12        | SPI1 RX                                           |
| NSS / CS    | GP13        | Chip select                                       |
| RESET       | GP15        | Active LOW reset                                  |
| DIO1        | GP14        | IRQ interrupt                                     |
| BUSY        | GP2         | Module busy indicator                             |
| RFEN        | GP3         | Combined LNA + PA enable — held HIGH at all times |
| TXEN        | ← DIO2      | Bridge on the module (see below)                  |
| VCC         | 3.3V        | Add a 100 µF capacitor close to the module        |
| GND         | GND         | —                                                 |

---

## E22**P** vs E22 — what changed

The E22**P**-868M30**S** is the newer revision of EBYTE's 30 dBm 868 MHz module. The pinout is interchangeable with the E22-900M30S but the RF switch control differs:

- On the older E22-900M30S, **RXEN** enables the LNA and **TXEN** enables the PA (two separate pins).
- On the E22**P**-868M30**S**, both functions are merged into a single **RFEN** pin acting as a global RF enable. RFEN must be held HIGH whenever the radio is active — both during RX and TX.

The firmware drives this with `SX126X_ANT_SW 3`, which sets GP3 (RFEN) HIGH once at boot and leaves it there. TXEN switching during transmit is still handled by the on-module DIO2 → TXEN bridge (see below).

---

## Special wiring: DIO2 → TXEN bridge on the E22P module

The E22P-868M30S does **not** connect DIO2 to the TXEN pin of its PA internally. They must be bridged with a short wire or solder bridge **on the module itself**:

```text
E22P DIO2 pin  ──┐
                 ├── wire / solder bridge on the module
E22P TXEN pin  ──┘
```

With this bridge in place, `SX126X_DIO2_AS_RF_SWITCH` causes the SX1262 to drive DIO2 HIGH automatically during TX, enabling the PA without needing an RP2350 GPIO for TXEN.

**Without this bridge the module will not transmit.**

---

## Build

```bash
pio run -e wiznet_5500_evb_pico2_e22p
```

### Flash — BOOTSEL mode

1. Hold the **BOOTSEL** button on the W5500-EVB-Pico2.
2. Connect USB to the PC — it appears as a `RPI-RP2` storage drive.
3. Copy the `.uf2` file:

```text
.pio/build/wiznet_5500_evb_pico2_e22p/firmware-wiznet_5500_evb_pico2_e22p-*.uf2
```

Or directly with picotool:

```bash
pio run -e wiznet_5500_evb_pico2_e22p -t upload
```

---

## Network usage

This board uses Ethernet (no Wi-Fi). From the Meshtastic app:

- **Enable Ethernet** under `Config → Network → Ethernet Enabled`
- **DHCP** by default; static IP can also be configured

Services available once connected:

| Service | Details                     |
| ------- | --------------------------- |
| NTP     | Time synchronization        |
| MQTT    | Messages to external broker |
| API     | TCP socket on port 4403     |
| Syslog  | Remote logging (optional)   |

---

## Technical notes

### LoRa — RF control

| Define                         | Effect                                                      |
| ------------------------------ | ----------------------------------------------------------- |
| `SX126X_ANT_SW 3`              | GP3 (RFEN) driven HIGH at init and never toggled again      |
| `SX126X_DIO2_AS_RF_SWITCH`     | SX1262 drives DIO2 HIGH during TX → enables TXEN via bridge |
| `SX126X_DIO3_TCXO_VOLTAGE 1.8` | E22P TCXO controlled by DIO3                                |
| `-D EBYTE_E22_900M30S`         | Sets `TX_GAIN_LORA=7`, max power 22 dBm                     |

### Ethernet

- Library: `arduino-libraries/Ethernet@^2.0.2` (supports W5100/W5200/W5500 auto-detection).
- SPI0 is explicitly initialized with pins GP16/18/19 before `Ethernet.init()`.
- DHCP timeout is set to 10 s (instead of the default 60 s) to avoid blocking LoRa startup.

### HW_VENDOR

Mapped to `meshtastic_HardwareModel_PRIVATE_HW` — no dedicated model exists in the Meshtastic protobuf for this hardware combination.
