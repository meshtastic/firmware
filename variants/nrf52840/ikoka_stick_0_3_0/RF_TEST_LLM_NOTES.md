# LLM Notes: IKOKA RF Test Firmware

This document is implementation context for future AI agents. Keep it separate from operator documentation.

## Hardware

- Variant env: `ikoka_stick_0_3_0`
- RF-test env: `ikoka_stick_0_3_0_rf_test`
- MCU: XIAO nRF52840
- Radio module: EBYTE E22-400M33S
- Radio chip: SX1268
- RF switch: DIO2 controls TXEN internally, MCU controls RXEN on D5
- TCXO: DIO3 at 2.2 V

## Key Files

- `variants/nrf52840/ikoka_stick_0_3_0/platformio.ini`
- `variants/nrf52840/ikoka_stick_0_3_0/variant.h`
- `src/configuration.h`
- `src/RFTestController.h`
- `src/RFTestController.cpp`
- `src/main.cpp`
- `src/mesh/SX126xInterface.h`
- `src/mesh/SX126xInterface.cpp`

## Design

`MESHTASTIC_RF_TEST_FIRMWARE` creates a special firmware path. It still lets normal boot initialize the board, config, service, and SX1268 radio, then it takes ownership of the returned radio interface before the router adds it to the mesh.

The RF-test build does not use the normal Meshtastic protobuf serial API for RF-test commands. `RFTestController` reads line-oriented text commands from USB CDC `Serial`.

The firmware boots idle. TX starts only after `START`.

## RF Modes

- `MODE LORA`: calls `SX126xInterface::startRfTestInfinitePreamble()`, which sends SX126x command `0xD2` (`SetTxInfinitePreamble`). This is the continuous LoRa-modulated test signal.
- `MODE CW`: calls `SX126xInterface::startRfTestContinuousWave()`, which uses RadioLib `transmitDirect()`. This is an unmodulated carrier.
- `STOP`: calls `SX126xInterface::stopRfTest()`.

## Power Model

`EBYTE_E22_400M33S` in `src/configuration.h` sets normal Meshtastic scaling:

- `TX_GAIN_LORA = 9`
- `SX126X_MAX_POWER = 21`

The RF-test controller accepts requested E22 module output power and applies a simple 12 dB PA model for chip power. It clamps chip power to `-9..21` dBm.

Do not change power scaling casually. E22 M33S modules can be damaged by bad RF load, excessive continuous duty, or incorrect PA/RF switch handling.
