# GAT562 product feature layer

## Official base

- Repository: `meshtastic/firmware`
- Commit: `d4aa7760ccffd100ddc7dacd6875e6eee5df4f11`
- Firmware version: 2.8.0
- Family target: `gat562_family`
- T9 target: `gat562_t9`

## Shared features

- GAT-IoT boot logo and 128x64 display layout.
- BLE advertising name fixed to `GAT562_XXXX` from the BLE MAC suffix.
- User-editable Meshtastic owner name remains independent from the BLE name.
- RSSI, SNR, and live radio noise display.
- Chinese UTF-8 display and pinyin candidate selection.
- Twenty editable preset messages.
- Four local games: CastleBoy, Snake, Blocks, and Breakout.
- Buzzer on P1.01 / Arduino pin 33.
- WS2812 notification LED on P0.29 / Arduino pin 29.
- GPS enable active-high on pin 34.
- Onboard BME280 telemetry.
- No external QSPI flash declaration.

## T9 additions

- TCA8418 interrupt: P0.20
- TCA8418 reset: P0.19
- DEL: P0.21
- USER: P0.09
- DEL short press: backspace
- DEL hold for two seconds: exit immediately
- USER in the editor: switch CN/EN
- T9 sequence: lowercase, uppercase, digit
- Key 1: comma, period, exclamation, question, less-than, greater-than, 1
- Key 0: space, 0
- Long OK in the editor: send

## Local build

```sh
platformio run -e gat562_family
platformio run -e gat562_t9
```

## GitHub Actions build

1. Open **Actions** and select **Build One Target**.
2. Select branch `gat-iot`.
3. Enter `gat562_family` or `gat562_t9` as the target.
4. Select `nrf52840` as the architecture and run the workflow.

Actions uses PlatformIO 6.1.19, the same version used for the local stable build.
The Nordic platform, framework commit, libraries, board definition, build flags, and
source commit are shared by local and Actions builds.
