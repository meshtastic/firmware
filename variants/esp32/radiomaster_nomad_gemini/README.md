# RadioMaster Nomad

This prototype target enables the primary LR1121 sub-GHz path and is initially validated for the US 902-928 MHz band. The second LR1121 is held in reset with its chip-select inactive until Meshtastic has a dual-radio/backhaul design; its reserved pins are CS 13, reset 21, DIO1 34, and busy 39.

The primary radio uses CS 27, reset 15, DIO1 37, busy 36, SCK 25, MOSI 32, and MISO 33. Its LR1121-controlled RF switch is:

| Mode             | DIO5 | DIO6 | DIO7 | DIO8 |
| ---------------- | ---- | ---- | ---- | ---- |
| Standby          | low  | low  | low  | low  |
| 900 MHz receive  | low  | low  | high | low  |
| 900 MHz transmit | low  | low  | low  | high |
| 2.4 GHz transmit | low  | high | low  | low  |
| Wi-Fi scan       | high | low  | low  | low  |

DIO8 enables the 900 MHz transmit path. APC2 on GPIO26 is applied immediately before transmission and cleared for receive, idle, and sleep. The provisional 915 MHz power table is:

| Total dBm | LR1121 dBm | APC2 DAC |
| --------: | ---------: | -------: |
|        10 |        -17 |      120 |
|        14 |        -16 |      120 |
|        17 |        -14 |      120 |
|        20 |        -11 |      120 |
|        24 |         -7 |      120 |
|        27 |         -3 |      120 |
|        30 |          5 |       95 |

GPIO2 controls the active-high fan. `lora.pa_fan_disabled` switches it fully off or on, with on as the default.

Meshtastic Wi-Fi runs on the primary ESP32. The currently unsupported ESP32-C3 Wi-Fi backpack is held in reset on GPIO19.

Pin assignments and RF behavior follow the local mLRS Nomad HAL and the [ExpressLRS RadioMaster Nomad target](https://github.com/ExpressLRS/targets/blob/master/TX/Radiomaster%20Nomad.json). The power table still requires bench verification before this prototype is promoted beyond support level 3.
