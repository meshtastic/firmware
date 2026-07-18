# GAT562 T9 feature layer

## Official base

- Repository: meshtastic/firmware
- Commit: d4aa7760ccffd100ddc7dacd6875e6eee5df4f11
- Firmware version: 2.8.0
- PlatformIO environment: gat562_t9

## Shared product features

- GAT-IoT boot logo and 128x64 GAT562 display layout.
- BLE advertising name fixed to GAT562_XXXX from the BLE MAC suffix.
  The Meshtastic node owner/name remains managed by the official app and
  configuration path.
- Read-only RSSI, SNR, and radio noise display.
- Chinese UTF-8 display and pinyin candidate selection.
- Four local games: CastleBoy, Snake, Blocks, and Breakout.
- Buzzer on P1.01 / Arduino pin 33:
  - key clicks use the official BuzzerFeedbackThread;
  - local keypad sends use a short non-blocking confirmation melody;
  - received messages use two repeats of the Nokia-style RTTTL melody.
- GPS enable is active-high on pin 34.
- No external QSPI flash is declared for this hardware.

## T9 hardware and behavior

- TCA8418 interrupt: P0.20
- TCA8418 reset: P0.19
- DEL: P0.21
- USER: P0.09
- DEL short press: backspace
- DEL hold for 2 seconds: exit immediately
- USER in the editor: switch CN/EN
- T9 sequence: lowercase, uppercase, then digit
- Key 1: comma, period, exclamation, question, less-than, greater-than, 1
- Key 0: space, then 0
- Long OK in the editor: send

## Build

    platformio run -e gat562_t9

Verified build:

- RAM: 99,180 / 248,832 bytes (39.9%)
- Flash: 788,280 / 815,104 bytes (96.7%)
- Official warm-node storage region guard: passed, 14 KB clear
- ISR and board-variant guards: passed
