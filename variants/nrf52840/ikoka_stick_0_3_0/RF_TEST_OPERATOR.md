# IKOKA RF Test Firmware

This is a dedicated lab firmware for IKOKA STICK 0.3.0 boards with the EBYTE E22-400M33S SX1268 module. It is not normal Meshtastic mesh firmware.

The RF-test build is:

```bash
pio run -e ikoka_stick_0_3_0_rf_test
```

The normal Meshtastic build is:

```bash
pio run -e ikoka_stick_0_3_0
```

## Safety

Use a 433 MHz dummy load or a correctly rated antenna before `START`.

The E22-400M33S can produce about 33 dBm. Continuous transmission can overheat the PA, exceed local regulations, or damage the module if the RF output is unterminated. Start at lower power when checking wiring and power supply behavior.

The firmware boots with TX stopped. It will not transmit until you send `START`.

## USB Commands

Connect to the USB CDC serial port at 115200 baud and send line-oriented commands:

```text
HELP
STATUS
FREQ 433.125
POWER 33
MODE LORA
MODE CW
MODE OFF
START
STOP
```

`MODE LORA` starts an SX126x infinite preamble when `START` is sent. This is a continuous LoRa-modulated signal.

`MODE CW` starts an unmodulated carrier when `START` is sent. This is useful for carrier power and frequency checks, but it is not LoRa-modulated.

`POWER` is requested module output power in dBm. The firmware prints both the requested E22 module output and the SX1268 chip power it applies.

The default frequency range for this lab build is 433.000 to 435.000 MHz.

## Suggested Lab Flow

1. Attach a 433 MHz dummy load or attenuator path.
2. Open USB serial at 115200 baud.
3. Send `STATUS`.
4. Set `FREQ <MHz>`, `POWER <dBm>`, and `MODE LORA` or `MODE CW`.
5. Send `START`.
6. Measure RF output.
7. Send `STOP` before changing hardware connections.
