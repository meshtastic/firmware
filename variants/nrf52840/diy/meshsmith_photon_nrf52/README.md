# MeshSmith Photon-1W nRF52 — Meshtastic test target

This variant is an experimental Meshtastic target for the **MeshSmith Photon-1W nRF52**.

## Hardware

- MCU: Seeed XIAO nRF52840
- Radio: Ebyte E22-900M30S (SX1262 + external PA)
- LoRa CS/NSS: D3
- LoRa DIO1: D0
- LoRa RESET: D1
- LoRa BUSY: D2
- SPI: D8/D9/D10
- I2C: D4/D5
- GPS: D6/D7 at 115200 baud
- RF switching: SX1262 DIO2
- TCXO: 1.8 V

## PA safety

MeshSmith's Photon firmware configuration limits the SX1262 drive to **20 dBm**, with the E22-900M30S PA producing approximately **30 dBm / 1 W** output. This target therefore defines:

- `SX126X_MAX_POWER=20`
- `TX_GAIN_LORA=10`

Do not remove that cap without RF bench testing.

## Status

**TEST / not hardware-validated for Meshtastic yet.**

Before full-power use, verify:

1. boot and USB serial
2. BLE connection
3. SX1262 initialization
4. LoRa receive
5. low-power transmit
6. RF output into a suitable attenuator/dummy load and spectrum analyzer
7. normal transmit
8. GPS
9. battery telemetry

The Photon MAX17048 fuel-gauge integration is not yet ported; this first target uses the XIAO VBAT ADC fallback.

## Build

```bash
pio run -e meshsmith_photon_nrf52
```

The expected UF2 output is:

```
.pio/build/meshsmith_photon_nrf52/firmware.uf2
```

For a factory-fresh XIAO nRF52840 bootloader, double-tap reset and copy the UF2 to the mounted XIAO bootloader drive.
