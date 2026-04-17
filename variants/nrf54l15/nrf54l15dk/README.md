# nRF54L15-DK — EBYTE E22-900M30S Wiring Guide

Board: **Nordic nRF54L15-DK (PCA10156)**
Radio: **EBYTE E22-900M30S** (SX1262, 30 dBm, 868/915 MHz)

---

## Why P2 (HP domain) and not P1

The nRF54L15 splits its GPIOs across three supply domains:

- **P0** — Main domain, **3.0 V** — usable
- **P1** — LP domain, **1.8 V** — **not compatible** with the SX1262
- **P2** — HP domain, **3.0 V** — usable

The SX1262 requires VIH ≥ 0.7 × VDD (≈ 2.31 V at VDD = 3.3 V). P1's 1.8 V output
leaves the chip stuck in reset with `BUSY` never going LOW. All E22 signals
therefore live on **P2** and are driven by **SPIM00**.

> `P2.09` is normally wired to LED0 on the DK; we ignore the LED and use
> SPIM00's default MISO pin. The on-board MX25R64 NOR flash also sat on SPIM00
> — it is deleted in the device-tree overlay to free the bus.

---

## Connections — J2 header, P2 bank

| E22-900M30S | GPIO  | DK pin | Function                                            |
|-------------|-------|--------|-----------------------------------------------------|
| MISO        | P2.04 | 36     | SPIM00 data in                                      |
| NSS / CS    | P2.05 | 37     | SPI chip-select (driven by RadioLib as a GPIO)      |
| DIO1        | P2.06 | 38     | IRQ — modem interrupt (routed via gpiote30)         |
| BUSY        | P2.03 | 35     | Module busy (GPIO input)                            |
| NRESET      | P2.00 | 32     | Module reset (GPIO output, active LOW)              |
| RXEN        | P2.07 | 39     | LNA enable — held HIGH via `SX126X_ANT_SW`          |
| MOSI        | P2.02 | 34     | SPIM00 data out                                     |
| SCK         | P2.01 | 33     | SPIM00 clock                                        |
| GND         | —     | GND    | Common ground                                       |
| VCC         | —     | VDD    | 3.3 V                                               |

> **Numbering convention**: `P0.n = n`, `P1.n = 16+n`, `P2.n = 32+n`.
> Example: `P2.04` → 32 + 4 = **36**.

---

## DIO2 → TXEN bridge (required)

The E22-900M30S does **not** connect DIO2 to TXEN internally. A physical bridge on the module is required:

1. Locate the `DIO2` and `TXEN` pads on the underside of the E22 module.
2. Solder a wire bridge or a 0 Ω resistor between the two pads.
3. With this bridge, the SX1262 drives the PA automatically via `SX126X_DIO2_AS_RF_SWITCH`.

Without this bridge the module **will not transmit** (PA is never enabled).

---

## RXEN — LNA always on

`RXEN` (P2.07) is held HIGH permanently via `SX126X_ANT_SW 39` in `variant.h`.
**Do not use** `SX126X_RXEN` — RadioLib would drive it LOW in IDLE state and
the LNA would stay disabled (radio deaf in RX).

---

## Reserved DK pins — do not reuse

| Pins         | Reserved function                                         |
|--------------|-----------------------------------------------------------|
| P0.00–P0.03  | IMCU debug UART (uart30, J-Link VCOM — used by RTT host) |
| P0.04        | BTN3                                                      |
| P1.00–P1.01  | 32 kHz crystal                                            |
| P1.02–P1.03  | NFC antenna                                               |
| P1.10        | LED1 (status LED — kept)                                  |
| P1.13        | BTN0 (only remaining user button)                         |
| P1.14        | LED3                                                      |
| P2.01–P2.05  | SPIM00 / E22 (see connection table above)                 |
| P2.08–P2.10  | Trace ETM / LED2 (avoid)                                  |

---

## Build and flash

```bash
# Build
pio run -e nrf54l15dk

# Flash (requires a J-Link connected via the DK's on-board IMCU)
pio run -e nrf54l15dk -t upload

# Monitor RTT (channel 1 = Meshtastic logs)
JLinkRTTLogger -device nRF54L15_xxAA -if SWD -speed 4000 -RTTChannel 1 boot.log
```

Expected boot log:

```text
*** Booting Zephyr OS build zephyr-v40201 ***
[nrf54l15] Reset cause: ...
[nrf54l15] B: calling setup()
INFO  | ... SX1262
INFO  | ... lora.begin() = 0          ← RADIOLIB_ERR_NONE
[nrf54l15] C: setup() returned
```

If you see `Record critical error 3` (`NO_RADIO`), check: DIO2→TXEN bridge,
supply voltages (the E22 must see 3.0–3.3 V on P2, not 1.8 V), and SPI wiring
continuity.
