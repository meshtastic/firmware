# RadioMaster Nomad

This experimental target initializes both LR1121 sub-GHz paths. The primary follows `config.lora`; the secondary is fixed to US ShortTurbo at 926.750 MHz, is enabled only when the primary region is `US`, and maps logical channel 1 to `TRANSPORT_LORA_ALT1`. The secondary pins are CS 13, reset 21, DIO1 34, and busy 39.

The primary radio uses CS 27, reset 15, DIO1 37, busy 36, SCK 25, MOSI 32, and MISO 33. Its LR1121-controlled RF switch is:

| Mode             | DIO5 | DIO6 | DIO7 | DIO8 |
| ---------------- | ---- | ---- | ---- | ---- |
| Standby          | low  | low  | low  | low  |
| 900 MHz receive  | low  | low  | high | low  |
| 900 MHz transmit | low  | low  | low  | high |
| 2.4 GHz transmit | low  | high | low  | low  |
| Wi-Fi scan       | high | low  | low  | low  |

DIO8 enables the 900 MHz transmit path. APC2 on GPIO26 is applied immediately before transmission and cleared for receive, idle, and sleep. This dual-radio review target enables both transmit paths, clamps them to 10 dBm, and serializes transmission. Do not test simultaneous 1 W operation. The provisional 915 MHz power table is:

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

See [the dual-radio experiment notes](../../../docs/radiomaster-nomad-dual-radio-experiment.md) for routing limitations and the staged RF-safety procedure.
