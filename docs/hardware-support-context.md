# Hardware Support Context

This document inventories the board-support inputs and common target-definition fields
currently used in the Meshtastic firmware repository. It is intended as reusable context
for adding new hardware support without re-discovering naming patterns, metadata keys, and
frequently used pin or capability macros from scratch.

## Scope

- Variant directories scanned: 166
- PlatformIO environments summarized: 212
- Sources: `variants/**/platformio.ini` and `variants/**/variant.h`
- Notes: This inventory reflects explicit per-variant declarations. Some boards also inherit
  defaults from architecture headers or shared base environments, which must still be checked
  before creating new hardware support.

## Repository Metadata Inputs

The following `custom_meshtastic_*` metadata keys are already used across board environments:

- `custom_meshtastic_hw_model`
- `custom_meshtastic_hw_model_slug`
- `custom_meshtastic_architecture`
- `custom_meshtastic_actively_supported`
- `custom_meshtastic_support_level`
- `custom_meshtastic_display_name`
- `custom_meshtastic_images`
- `custom_meshtastic_tags`
- `custom_meshtastic_requires_dfu`
- `custom_meshtastic_partition_scheme`

## Common Target-Definition Categories

### Input

| Macro                              | Used in      | Description                                                                 |
| ---------------------------------- | ------------ | --------------------------------------------------------------------------- |
| `BUTTON_PIN`                       | 127 variants | Primary user button GPIO pin number.                                        |
| `ALT_BUTTON_PIN`                   | 12 variants  | Secondary / alternate button pin, used on boards with two buttons.          |
| `CANCEL_BUTTON_PIN`                | 8 variants   | Button wired to cancel/back actions (e.g., T-Deck cancel key).              |
| `KB_BL_PIN`                        | 5 variants   | Keyboard backlight control pin (e.g., T-LoRa Pager keyboard).               |
| `KB_INT`                           | 4 variants   | Keyboard interrupt input pin; signals a keypress to the MCU.                |
| `INPUTDRIVER_ENCODER_TYPE`         | 3 variants   | Selects the rotary encoder driver variant (0 = none, 1+ = specific type).   |
| `BUTTON_PIN_ALT`                   | 2 variants   | Alternative spelling used by some older board families for a second button. |
| `INPUTDRIVER_TWO_WAY_ROCKER`       | 2 variants   | Enables the two-way rocker input driver.                                    |
| `INPUTDRIVER_TWO_WAY_ROCKER_RIGHT` | 2 variants   | GPIO pin for the rightward rocker direction.                                |
| `INPUTDRIVER_TWO_WAY_ROCKER_LEFT`  | 2 variants   | GPIO pin for the leftward rocker direction.                                 |
| `INPUTDRIVER_TWO_WAY_ROCKER_BTN`   | 2 variants   | GPIO pin for the rocker click/press direction.                              |
| `KB_POWERON`                       | 2 variants   | Pin used to power-on or enable the keyboard peripheral.                     |
| `KB_SLAVE_ADDRESS`                 | 2 variants   | I2C address of the keyboard controller IC.                                  |
| `ROTARY_A`                         | 2 variants   | First encoder channel pin (clock / A signal).                               |
| `ROTARY_B`                         | 2 variants   | Second encoder channel pin (data / B signal).                               |

### Radio

| Macro                      | Used in      | Description                                                                   |
| -------------------------- | ------------ | ----------------------------------------------------------------------------- |
| `SX126X_CS`                | 150 variants | SX126x chip-select pin (often same as LORA_CS).                               |
| `SX126X_RESET`             | 150 variants | SX126x reset pin (often same as LORA_RESET).                                  |
| `SX126X_BUSY`              | 150 variants | SX126x BUSY pin; must be polled low before issuing SPI commands.              |
| `SX126X_DIO1`              | 150 variants | SX126x DIO1 interrupt output used for all IRQ events.                         |
| `USE_SX1262`               | 145 variants | Select the SX1262 sub-GHz LoRa chip driver.                                   |
| `SX126X_DIO3_TCXO_VOLTAGE` | 140 variants | Drives the TCXO regulator via DIO3; set to the supply voltage (e.g., 1.8).    |
| `LORA_CS`                  | 137 variants | LoRa radio SPI chip-select GPIO pin.                                          |
| `LORA_RESET`               | 135 variants | LoRa radio hardware-reset GPIO pin (active low).                              |
| `SX126X_DIO2_AS_RF_SWITCH` | 135 variants | Set to 1 so the driver controls the TX/RX RF switch via DIO2.                 |
| `LORA_DIO1`                | 130 variants | LoRa radio DIO1 interrupt pin; on SX126x this is the primary IRQ line.        |
| `LORA_SCK`                 | 125 variants | LoRa radio SPI clock pin.                                                     |
| `LORA_MISO`                | 125 variants | LoRa radio SPI MISO pin.                                                      |
| `LORA_MOSI`                | 125 variants | LoRa radio SPI MOSI pin.                                                      |
| `LORA_DIO2`                | 110 variants | LoRa radio DIO2 pin; on RF95 used for FSK interrupt; on SX126x tx/rx control. |
| `LORA_DIO0`                | 98 variants  | LoRa radio DIO0 interrupt pin (RF95/SX1276 done/RxDone).                      |

### GPS

| Macro                     | Used in      | Description                                                                    |
| ------------------------- | ------------ | ------------------------------------------------------------------------------ |
| `GPS_RX_PIN`              | 114 variants | UART RX pin connected to the GPS module TX output.                             |
| `GPS_TX_PIN`              | 111 variants | UART TX pin connected to the GPS module RX input.                              |
| `HAS_GPS`                 | 69 variants  | Set to 1 if the board has an on-board GPS receiver; 0 to disable GPS entirely. |
| `PIN_GPS_PPS`             | 43 variants  | GPS pulse-per-second input pin for timing synchronisation.                     |
| `PIN_GPS_EN`              | 30 variants  | GPIO to power-enable or power-gate the GPS module.                             |
| `PIN_GPS_STANDBY`         | 29 variants  | Places the GPS into standby/low-power mode when driven.                        |
| `GPS_THREAD_INTERVAL`     | 28 variants  | Millisecond poll interval for the GPS background thread.                       |
| `GPS_L76K`                | 27 variants  | Selects the Quectel L76K GPS driver and protocol.                              |
| `GPS_BAUDRATE`            | 27 variants  | UART baud rate for GPS serial communication.                                   |
| `GPS_EN_ACTIVE`           | 17 variants  | Logic level (HIGH or LOW) that enables the GPS power pin.                      |
| `PIN_GPS_RESET`           | 16 variants  | Hardware-reset line to the GPS module.                                         |
| `GPS_DEFAULT_NOT_PRESENT` | 16 variants  | Compiled-in default assuming no GPS; overridden at runtime if detected.        |
| `GPS_RESET_MODE`          | 13 variants  | Defines the reset signal polarity or protocol for the GPS chip.                |
| `GPS_UBLOX`               | 8 variants   | Selects the u-blox GPS driver and UBX protocol.                                |
| `PIN_GPS_REINIT`          | 7 variants   | Pin used to trigger GPS re-initialisation sequences.                           |

### Display

| Macro                         | Used in     | Description                                                              |
| ----------------------------- | ----------- | ------------------------------------------------------------------------ |
| `PIN_EINK_CS`                 | 51 variants | E-ink display SPI chip-select pin.                                       |
| `PIN_EINK_BUSY`               | 51 variants | E-ink display BUSY output; high when a page update is in progress.       |
| `PIN_EINK_DC`                 | 51 variants | E-ink display data/command select pin.                                   |
| `PIN_EINK_RES`                | 51 variants | E-ink display hardware-reset pin.                                        |
| `PIN_EINK_SCLK`               | 49 variants | E-ink display SPI clock pin.                                             |
| `PIN_EINK_MOSI`               | 49 variants | E-ink display SPI MOSI pin.                                              |
| `HAS_SCREEN`                  | 34 variants | Set to 1 if the board has any display hardware.                          |
| `USE_TFTDISPLAY`              | 27 variants | Selects the TFT LCD driver path.                                         |
| `TFT_HEIGHT`                  | 25 variants | Vertical pixel resolution of the TFT display.                            |
| `TFT_WIDTH`                   | 25 variants | Horizontal pixel resolution of the TFT display.                          |
| `SCREEN_TRANSITION_FRAMERATE` | 23 variants | Target framerate for UI animations and transitions.                      |
| `TFT_OFFSET_X`                | 22 variants | Horizontal pixel offset for display alignment correction.                |
| `TFT_OFFSET_Y`                | 22 variants | Vertical pixel offset for display alignment correction.                  |
| `SCREEN_ROTATE`               | 18 variants | Non-zero value rotates the display 180° for mounted-upside-down screens. |
| `TFT_BL`                      | 15 variants | Backlight control PWM or GPIO pin for the TFT panel.                     |

### I2C/SPI

| Macro                   | Used in      | Description                                           |
| ----------------------- | ------------ | ----------------------------------------------------- |
| `I2C_SDA`               | 103 variants | Primary I2C data line GPIO pin.                       |
| `I2C_SCL`               | 103 variants | Primary I2C clock line GPIO pin.                      |
| `PIN_SPI_MISO`          | 72 variants  | Primary SPI MISO (data from peripheral) GPIO pin.     |
| `PIN_SPI_MOSI`          | 72 variants  | Primary SPI MOSI (data to peripheral) GPIO pin.       |
| `PIN_SPI_SCK`           | 72 variants  | Primary SPI clock GPIO pin.                           |
| `SPI_INTERFACES_COUNT`  | 66 variants  | Number of hardware SPI buses available on this board. |
| `WIRE_INTERFACES_COUNT` | 64 variants  | Number of hardware I2C buses available on this board. |
| `PIN_SPI1_MISO`         | 35 variants  | Secondary SPI bus MISO pin.                           |
| `PIN_SPI1_MOSI`         | 35 variants  | Secondary SPI bus MOSI pin.                           |
| `PIN_SPI1_SCK`          | 35 variants  | Secondary SPI bus clock pin.                          |
| `SPI_FREQUENCY`         | 24 variants  | Default SPI clock frequency in Hz for this board.     |
| `SPI_READ_FREQUENCY`    | 21 variants  | Reduced SPI clock rate used for read transactions.    |
| `SPI_SCK`               | 19 variants  | SPI clock pin alias used in older board files.        |
| `SPI_MOSI`              | 19 variants  | SPI MOSI pin alias used in older board files.         |
| `SPI_MISO`              | 19 variants  | SPI MISO pin alias used in older board files.         |

### Power

| Macro                           | Used in      | Description                                                                |
| ------------------------------- | ------------ | -------------------------------------------------------------------------- |
| `BATTERY_PIN`                   | 128 variants | ADC input GPIO connected to the battery voltage divider.                   |
| `ADC_MULTIPLIER`                | 122 variants | Floating-point scale factor to convert raw ADC reading to battery voltage. |
| `ADC_CHANNEL`                   | 71 variants  | ADC channel enum or number for the battery sense input.                    |
| `BATTERY_SENSE_RESOLUTION_BITS` | 64 variants  | ADC resolution in bits used for battery voltage sampling.                  |
| `ADC_RESOLUTION`                | 49 variants  | Board-level ADC resolution definition, referenced by other power macros.   |
| `BATTERY_SENSE_RESOLUTION`      | 44 variants  | Alias for the effective ADC resolution for battery sense.                  |
| `ADC_CTRL`                      | 29 variants  | GPIO that enables or gates the ADC voltage-divider circuit.                |
| `ADC_CTRL_ENABLED`              | 27 variants  | Logic level (HIGH or LOW) that turns on the ADC control switch.            |
| `EXT_NOTIFY_OUT`                | 23 variants  | GPIO output used to signal an external LED or buzzer for notifications.    |
| `USE_POWERSAVE`                 | 21 variants  | Enables aggressive power-save mode (deep sleep, reduced poll intervals).   |
| `SLEEP_TIME`                    | 21 variants  | Default light-sleep duration in milliseconds between wakeups.              |
| `ADC_ATTENUATION`               | 20 variants  | ESP32 ADC input attenuation setting; controls measurable voltage range.    |
| `PIN_POWER_EN`                  | 18 variants  | GPIO to assert to enable a board power rail or load switch.                |
| `BATTERY_SENSE_SAMPLES`         | 11 variants  | Number of ADC samples to average for a stable battery reading.             |
| `HAS_PPM`                       | 4 variants   | Set to 1 if the board has an IP5306 or similar PPM power path IC.          |

### Connectivity/Other

| Macro             | Used in     | Description                                                         |
| ----------------- | ----------- | ------------------------------------------------------------------- |
| `HAS_TOUCHSCREEN` | 19 variants | Set to 1 for boards with a capacitive or resistive touch panel.     |
| `HAS_NEOPIXEL`    | 14 variants | Set to 1 if the board has addressable RGB LEDs (WS2812 / NeoPixel). |
| `PCF8563_RTC`     | 11 variants | I2C address of the PCF8563 real-time clock IC.                      |
| `HAS_ETHERNET`    | 9 variants  | Set to 1 for boards with a wired Ethernet interface.                |
| `HAS_I2S`         | 7 variants  | Set to 1 if I2S audio output is present.                            |
| `PCF85063_RTC`    | 3 variants  | I2C address of the PCF85063 real-time clock IC.                     |
| `NFC_INT`         | 2 variants  | Interrupt pin from the NFC controller IC.                           |
| `NFC_CS`          | 2 variants  | SPI chip-select for the NFC controller.                             |
| `USE_XL9555`      | 2 variants  | Enables the XL9555 16-bit I2C GPIO expander driver.                 |
| `EXPANDS_DRV_EN`  | 2 variants  | GPIO expander pin used to enable the haptic driver.                 |
| `EXPANDS_AMP_EN`  | 2 variants  | GPIO expander pin used to power-on the audio amplifier.             |
| `EXPANDS_KB_RST`  | 2 variants  | GPIO expander pin used to reset the keyboard controller.            |
| `EXPANDS_LORA_EN` | 2 variants  | GPIO expander pin used to power-gate the LoRa radio.                |
| `EXPANDS_GPS_EN`  | 2 variants  | GPIO expander pin used to power-gate the GPS module.                |
| `EXPANDS_NFC_EN`  | 2 variants  | GPIO expander pin used to power-gate the NFC controller.            |

### Other

| Macro                | Used in     | Description                                                                |
| -------------------- | ----------- | -------------------------------------------------------------------------- |
| `LED_POWER`          | 94 variants | GPIO for the status LED, defines the LED pin number.                       |
| `LED_STATE_ON`       | 88 variants | Logic level (HIGH or LOW) that turns the status LED on.                    |
| `PIN_SERIAL1_RX`     | 67 variants | Secondary UART RX pin (used for accessories, GPS on some boards).          |
| `PIN_SERIAL1_TX`     | 67 variants | Secondary UART TX pin.                                                     |
| `PIN_WIRE_SDA`       | 64 variants | Arduino-framework I2C SDA pin alias (nRF52 / RP2040 style).                |
| `PIN_WIRE_SCL`       | 64 variants | Arduino-framework I2C SCL pin alias.                                       |
| `PIN_LED1`           | 63 variants | First LED GPIO pin in the nRF52 Arduino BSP pin table.                     |
| `VARIANT_MCK`        | 63 variants | Crystal oscillator frequency in Hz for nRF52 variant clock configuration.  |
| `PINS_COUNT`         | 63 variants | Total number of GPIO pins defined in the Arduino BSP variant table.        |
| `NUM_DIGITAL_PINS`   | 63 variants | Count of digital-capable pins in the BSP variant table.                    |
| `NUM_ANALOG_INPUTS`  | 63 variants | Count of analog-input pins in the BSP variant table.                       |
| `NUM_ANALOG_OUTPUTS` | 63 variants | Count of analog-output (DAC) pins in the BSP variant table.                |
| `LED_BLUE`           | 62 variants | GPIO number of the blue status LED (typical on nRF52 and RP2040 boards).   |
| `USE_LFXO`           | 58 variants | Instructs the nRF52 BSP to use the low-frequency crystal oscillator.       |
| `BUTTON_NEED_PULLUP` | 54 variants | Enables internal pull-up on the button GPIO (duplicate entry for clarity). |

## Architecture and Environment Inventory

### diy

| Environment               | Display Name | HW Model | HW Slug | Variant Dir                                   | Common Categories                                         |
| ------------------------- | ------------ | -------- | ------- | --------------------------------------------- | --------------------------------------------------------- |
| 9m2ibr_aprs_lora_tracker  |              |          |         | variants/esp32/diy/9m2ibr_aprs_lora_tracker   | Display:1, Radio:24, Input:1, GPS:2, Power:4              |
| esp32c3_super_mini        |              |          |         | variants/esp32c3/diy/esp32c3_super_mini       | Display:1, Radio:18, Input:1, GPS:2                       |
| meshtastic-diy-v1_1       |              |          |         | variants/esp32/diy/v1_1                       | Radio:24, Input:1, GPS:1, Power:1                         |
| my-esp32s3-diy-eink       |              |          |         | variants/esp32s3/diy/my_esp32s3_diy_eink      | Display:6, Radio:17, Input:1, GPS:1, Connectivity/Other:1 |
| my-esp32s3-diy-oled       |              |          |         | variants/esp32s3/diy/my_esp32s3_diy_oled      | Display:1, Radio:17, Input:1, GPS:1, Connectivity/Other:1 |
| nrf52_promicro_diy-inkhud |              |          |         | variants/nrf52840/diy/nrf52_promicro_diy_tcxo | Display:4, Radio:29, Input:1, GPS:4, Power:6              |
| t-energy-s3_e22           |              |          |         | variants/esp32s3/diy/t-energy-s3_e22          | Display:1, Radio:18, Input:1, GPS:3, Power:3              |

### esp32

| Environment                 | Display Name                | HW Model | HW Slug                     | Variant Dir                                | Common Categories                                                  |
| --------------------------- | --------------------------- | -------- | --------------------------- | ------------------------------------------ | ------------------------------------------------------------------ |
| betafpv_2400_tx_micro       |                             |          |                             | variants/esp32/betafpv_2400_tx_micro       | Radio:14, Input:1, Connectivity/Other:1                            |
| betafpv_900_tx_nano         |                             |          |                             | variants/esp32/betafpv_900_tx_nano         | Display:1, Radio:10, Input:1                                       |
| chatter2                    |                             |          |                             | variants/esp32/chatter2                    | Display:12, Radio:16, Input:3, GPS:3, Power:4                      |
| hackerboxes-esp32-io        |                             |          |                             | variants/esp32/hackerboxes_esp32_io        | Display:1, Radio:16, Input:1, GPS:1                                |
| heltec-v1                   | Heltec V1                   | 11       | HELTEC_V1                   | variants/esp32/heltec_v1                   | Radio:5, Input:1, GPS:2, Power:3                                   |
| heltec-v2_0                 | Heltec V2.0                 | 5        | HELTEC_V2_0                 | variants/esp32/heltec_v2                   | Radio:5, Input:1, GPS:2, Power:3                                   |
| heltec-v2_1                 | Heltec V2.1                 | 10       | HELTEC_V2_1                 | variants/esp32/heltec_v2.1                 | Radio:5, Input:1, GPS:3, Power:4                                   |
| heltec-wireless-bridge      |                             |          |                             | variants/esp32/heltec_wireless_bridge      | Radio:9, GPS:1                                                     |
| heltec-wsl-v2_1             |                             |          |                             | variants/esp32/heltec_wsl_v2.1             | Radio:9, Input:1, Power:5                                          |
| hydra                       | Hydra                       | 39       | HYDRA                       | variants/esp32/diy/hydra                   | Radio:22, Input:1, GPS:3, Power:4                                  |
| m5stack-core                | M5 Stack                    | 42       | M5STACK                     | variants/esp32/m5stack_core                | Display:7, Radio:9, Input:1, GPS:2                                 |
| m5stack-coreink             |                             |          |                             | variants/esp32/m5stack_coreink             | Display:7, Radio:34, Input:1, GPS:2, Power:3, Connectivity/Other:1 |
| meshtastic-diy-v1           | DIY V1                      | 39       | DIY_V1                      | variants/esp32/diy/v1                      | Radio:23, Input:1, GPS:3, Power:4                                  |
| meshtastic-dr-dev           | DR-DEV                      | 41       | DR_DEV                      | variants/esp32/diy/dr-dev                  | Display:1, Radio:26, Input:1, GPS:2, Power:3                       |
| nano-g1                     | Nano G1                     | 14       | NANO_G1                     | variants/esp32/nano-g1                     | Display:1, Radio:13, Input:1, GPS:2, Power:1                       |
| nano-g1-explorer            | Nano G1 Explorer            | 17       | NANO_G1_EXPLORER            | variants/esp32/nano-g1-explorer            | Display:1, Radio:13, Input:1, GPS:2, Power:5                       |
| radiomaster_900_bandit      |                             |          |                             | variants/esp32/radiomaster_900_bandit      | Radio:19, Input:1, GPS:1, Connectivity/Other:1                     |
| radiomaster_900_bandit_nano | RadioMaster 900 Bandit Nano | 64       | RADIOMASTER_900_BANDIT_NANO | variants/esp32/radiomaster_900_bandit_nano | Display:1, Radio:19                                                |
| rak11200                    | RAK WisBlock 11200          | 13       | RAK11200                    | variants/esp32/rak11200                    | Radio:18, GPS:2, Power:2                                           |
| station-g1                  | Station G1                  | 25       | STATION_G1                  | variants/esp32/station-g1                  | Display:1, Radio:13, Input:1, GPS:2, Power:5                       |
| sugarcube                   |                             |          |                             | variants/esp32/tlora_v2_1_16               | Radio:6, Power:4                                                   |
| tbeam                       | LILYGO T-Beam               | 4        | TBEAM                       | variants/esp32/tbeam                       | Display:4, Radio:14, Input:1, GPS:3, Power:2, Connectivity/Other:1 |
| tbeam-displayshield         |                             |          |                             | variants/esp32/tbeam                       | Display:4, Radio:14, Input:1, GPS:3, Power:2, Connectivity/Other:1 |
| tbeam0_7                    | LILYGO T-Beam V0.7          | 6        | TBEAM_V0P7                  | variants/esp32/tbeam_v07                   | Radio:5, Input:1, GPS:3, Power:3                                   |
| tlora-v1                    | LILYGO T-LoRa V1            | 2        | TLORA_V1                    | variants/esp32/tlora_v1                    | Radio:5, Input:1, Power:1                                          |
| tlora-v2                    | LILYGO T-LoRa V2            | 1        | TLORA_V2                    | variants/esp32/tlora_v2                    | Radio:5, Input:1, Power:2                                          |
| tlora-v2-1-1_6              | LILYGO T-LoRa V2.1-1.6      | 3        | TLORA_V2_1_1P6              | variants/esp32/tlora_v2_1_16               | Radio:6, Power:4                                                   |
| tlora-v2-1-1_8              | LILYGO T-LoRa V2.1-1.8      | 15       | TLORA_V2_1_1P8              | variants/esp32/tlora_v2_1_18               | Radio:6, Input:1, Power:3                                          |
| tlora_v1_3                  |                             |          |                             | variants/esp32/tlora_v1_3                  | Radio:5, Input:1, Power:2                                          |
| trackerd                    |                             |          |                             | variants/esp32/trackerd                    | Display:1, Radio:5, Input:1, GPS:9, Power:4                        |
| wiphone                     |                             |          |                             | variants/esp32/wiphone                     | Display:9, Radio:9, GPS:1                                          |

### esp32-c3

| Environment                | Display Name | HW Model | HW Slug     | Variant Dir                               | Common Categories                                                  |
| -------------------------- | ------------ | -------- | ----------- | ----------------------------------------- | ------------------------------------------------------------------ |
| ai-c3                      |              |          |             | variants/esp32c3/ai-c3                    | Radio:15, Input:1, GPS:1                                           |
| hackerboxes-esp32c3-oled   |              |          |             | variants/esp32c3/hackerboxes_esp32c3_oled | Display:1, Radio:16, Input:1, GPS:1                                |
| heltec-hru-3601            |              |          |             | variants/esp32c3/heltec_hru_3601          | Display:1, Radio:16, Input:1, GPS:1, Power:1, Connectivity/Other:1 |
| heltec-ht62-esp32c3-sx1262 | Heltec HT62  | 53       | HELTEC_HT62 | variants/esp32c3/heltec_esp32c3           | Display:1, Radio:16, Input:1, GPS:1                                |
| m5stack-stamp-c3           |              |          |             | variants/esp32c3/m5stack-stamp-c3         | Radio:9, Input:1, GPS:1                                            |

### esp32-c6

| Environment     | Display Name     | HW Model | HW Slug     | Variant Dir                      | Common Categories                                |
| --------------- | ---------------- | -------- | ----------- | -------------------------------- | ------------------------------------------------ |
| m5stack-unitc6l | M5Stack Unit C6L | 111      | M5STACK_C6L | variants/esp32c6/m5stack_unitc6l | Display:1, Radio:14, GPS:3, Connectivity/Other:1 |
| tlora-c6        |                  |          |             | variants/esp32c6/tlora_c6        | Radio:15                                         |

### esp32-s3

| Environment                      | Display Name                  | HW Model | HW Slug                      | Variant Dir                                   | Common Categories                                                    |
| -------------------------------- | ----------------------------- | -------- | ---------------------------- | --------------------------------------------- | -------------------------------------------------------------------- |
| CDEBYTE_EoRa-Hub                 |                               |          |                              | variants/esp32s3/CDEBYTE_EoRa-Hub             | Display:2, Radio:14, Input:1, Power:6                                |
| CDEBYTE_EoRa-S3                  | EBYTE EoRa-S3                 | 61       | CDEBYTE_EORA_S3              | variants/esp32s3/CDEBYTE_EoRa-S3              | Display:2, Radio:13, Input:1, Power:3                                |
| EBYTE_ESP32-S3                   |                               |          |                              | variants/esp32s3/EBYTE_ESP32-S3               | Display:1, Radio:26, Input:1, GPS:5, Power:1                         |
| ESP32-S3-Pico                    |                               |          |                              | variants/esp32s3/esp32-s3-pico                | Display:6, Radio:18, Input:2, GPS:1, Power:4, Connectivity/Other:1   |
| bpi_picow_esp32_s3               |                               |          |                              | variants/esp32s3/bpi_picow_esp32_s3           | Display:1, Radio:25, Input:1, GPS:1                                  |
| crowpanel-esp32s3-2-epaper       |                               |          |                              | variants/esp32s3/crowpanel-esp32s3-5-epaper   | Display:6, Radio:21, Input:1, GPS:1, Power:1                         |
| crowpanel-esp32s3-4-epaper       |                               |          |                              | variants/esp32s3/crowpanel-esp32s3-5-epaper   | Display:6, Radio:21, Input:1, GPS:1, Power:1                         |
| crowpanel-esp32s3-5-epaper       |                               |          |                              | variants/esp32s3/crowpanel-esp32s3-5-epaper   | Display:6, Radio:21, Input:1, GPS:1, Power:1                         |
| dreamcatcher-2206                |                               |          |                              | variants/esp32s3/dreamcatcher                 | Radio:16, Input:1, Power:1, Connectivity/Other:1                     |
| elecrow-adv-24-28-tft            | Crowpanel Adv 2.4/2.8 TFT     | 97       | CROWPANEL                    | variants/esp32s3/elecrow_panel                | Display:1, Radio:21, GPS:6, Power:2                                  |
| elecrow-adv-35-tft               | Crowpanel Adv 3.5 TFT         | 97       | CROWPANEL                    | variants/esp32s3/elecrow_panel                | Display:1, Radio:21, GPS:6, Power:2                                  |
| elecrow-adv1-43-50-70-tft        | Crowpanel Adv 4.3/5.0/7.0 TFT | 97       | CROWPANEL                    | variants/esp32s3/elecrow_panel                | Display:1, Radio:21, GPS:6, Power:2                                  |
| hackaday-communicator            |                               |          |                              | variants/esp32s3/hackaday-communicator        | Display:12, Radio:14, Input:1, GPS:1, Power:2                        |
| heltec-v3                        | Heltec V3                     | 43       | HELTEC_V3                    | variants/esp32s3/heltec_v3                    | Display:1, Radio:16, Input:1, Power:6                                |
| heltec-v4                        | Heltec V4                     | 110      | HELTEC_V4                    | variants/esp32s3/heltec_v4                    | Display:1, Radio:21, Input:1, GPS:10, Power:6                        |
| heltec-v4-tft                    | Heltec V4 TFT                 | 110      | HELTEC_V4                    | variants/esp32s3/heltec_v4                    | Display:1, Radio:21, Input:1, GPS:10, Power:6                        |
| heltec-vision-master-e213        | Heltec Vision Master E213     | 67       | HELTEC_VISION_MASTER_E213    | variants/esp32s3/heltec_vision_master_e213    | Display:6, Radio:15, Input:2, Power:6                                |
| heltec-vision-master-e213-inkhud |                               |          |                              | variants/esp32s3/heltec_vision_master_e213    | Display:6, Radio:15, Input:2, Power:6                                |
| heltec-vision-master-e290        | Heltec Vision Master E290     | 68       | HELTEC_VISION_MASTER_E290    | variants/esp32s3/heltec_vision_master_e290    | Display:6, Radio:15, Input:2, Power:6                                |
| heltec-vision-master-e290-inkhud |                               |          |                              | variants/esp32s3/heltec_vision_master_e290    | Display:6, Radio:15, Input:2, Power:6                                |
| heltec-vision-master-t190        | Heltec Vision Master T190     | 66       | HELTEC_VISION_MASTER_T190    | variants/esp32s3/heltec_vision_master_t190    | Display:6, Radio:16, Input:2, Power:6                                |
| heltec-wireless-paper            | Heltec Wireless Paper         | 49       | HELTEC_WIRELESS_PAPER        | variants/esp32s3/heltec_wireless_paper        | Display:6, Radio:15, Input:1, Power:6                                |
| heltec-wireless-paper-inkhud     |                               |          |                              | variants/esp32s3/heltec_wireless_paper        | Display:6, Radio:15, Input:1, Power:6                                |
| heltec-wireless-paper-v1_0       | Heltec Wireless Paper V1.0    | 57       | HELTEC_WIRELESS_PAPER_V1_0   | variants/esp32s3/heltec_wireless_paper_v1     | Display:6, Radio:15, Input:1, Power:6                                |
| heltec-wireless-tracker          | Heltec Wireless Tracker V1.1  | 48       | HELTEC_WIRELESS_TRACKER      | variants/esp32s3/heltec_wireless_tracker      | Display:9, Radio:16, Input:1, GPS:7, Power:6                         |
| heltec-wireless-tracker-V1-0     | Heltec Wireless Tracker V1.0  | 58       | HELTEC_WIRELESS_TRACKER_V1_0 | variants/esp32s3/heltec_wireless_tracker_V1_0 | Display:9, Radio:16, Input:1, GPS:9, Power:4                         |
| heltec-wireless-tracker-v2       | Heltec Wireless Tracker V2    | 113      | HELTEC_WIRELESS_TRACKER_V2   | variants/esp32s3/heltec_wireless_tracker_v2   | Display:10, Radio:19, Input:1, GPS:7, Power:6                        |
| heltec-wsl-v3                    | Heltec Wireless Stick Lite V3 | 44       | HELTEC_WSL_V3                | variants/esp32s3/heltec_wsl_v3                | Radio:16, Input:1, Power:6                                           |
| heltec_capsule_sensor_v3         |                               |          |                              | variants/esp32s3/heltec_capsule_sensor_v3     | Display:1, Radio:16, Input:1, GPS:7, Power:6                         |
| heltec_sensor_hub                |                               |          |                              | variants/esp32s3/heltec_sensor_hub            | Display:1, Radio:15, Input:1, Power:6, Connectivity/Other:1          |
| icarus                           |                               |          |                              | variants/esp32s3/icarus                       | Display:1, Radio:12, Input:1                                         |
| link32-s3-v1                     |                               |          |                              | variants/esp32s3/link32_s3_v1                 | Display:1, Radio:16, Input:2, Power:4, Connectivity/Other:1          |
| m5stack-cardputer-adv            |                               |          |                              | variants/esp32s3/m5stack_cardputer_adv        | Display:5, Radio:17, Input:2, GPS:4, Power:3, Connectivity/Other:2   |
| m5stack-cores3                   |                               |          |                              | variants/esp32s3/m5stack_cores3               | Radio:11, Power:1                                                    |
| mesh-tab-3-2-IPS-capacitive      |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-3-2-IPS-resistive       |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-3-2-TN-resistive        |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-3-5-IPS-capacitive      |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-3-5-IPS-resistive       |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-3-5-TN-resistive        |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mesh-tab-4-0-IPS-capacitive      |                               |          |                              | variants/esp32s3/mesh-tab                     | Radio:18, Input:1, GPS:2, Power:5, Connectivity/Other:1              |
| mini-epaper-s3                   | LILYGO Mini ePaper S3 E-Ink   |          | MINI_EPAPER_S3               | variants/esp32s3/mini-epaper-s3               | Display:8, Radio:12, Input:5, GPS:1, Power:3, Connectivity/Other:1   |
| mini-epaper-s3-inkhud            |                               |          |                              | variants/esp32s3/mini-epaper-s3               | Display:8, Radio:12, Input:5, GPS:1, Power:3, Connectivity/Other:1   |
| nibble-esp32                     |                               |          |                              | variants/esp32s3/nibble_esp32                 | Radio:9, Input:1                                                     |
| nugget-s3-lora                   |                               |          |                              | variants/esp32s3/nugget_s3_lora               | Display:2, Radio:9, Input:1, Connectivity/Other:1                    |
| picomputer-s3                    | Pi Computer S3                | 52       | PICOMPUTER_S3                | variants/esp32s3/picomputer-s3                | Display:10, Radio:9, Input:1, Power:3                                |
| picomputer-s3-tft                |                               |          |                              | variants/esp32s3/picomputer-s3                | Display:10, Radio:9, Input:1, Power:3                                |
| rak3112                          |                               |          |                              | variants/esp32s3/rak3312                      | Radio:13, GPS:3, Power:5                                             |
| rak3312                          | RAK3312                       | 106      | RAK3312                      | variants/esp32s3/rak3312                      | Radio:13, GPS:3, Power:5                                             |
| rak_wismesh_tap_v2               | RAK WisMesh Tap V2            | 116      | WISMESH_TAP_V2               | variants/esp32s3/rak_wismesh_tap_v2           | Radio:13, Input:1, GPS:3, Power:4                                    |
| rak_wismesh_tap_v2-tft           |                               |          |                              | variants/esp32s3/rak_wismesh_tap_v2           | Radio:13, Input:1, GPS:3, Power:4                                    |
| seeed-sensecap-indicator         | Seeed SenseCAP Indicator      | 70       | SENSECAP_INDICATOR           | variants/esp32s3/seeed-sensecap-indicator     | Display:12, Radio:17, Input:1, GPS:4, Connectivity/Other:1           |
| seeed-sensecap-indicator-tft     |                               |          |                              | variants/esp32s3/seeed-sensecap-indicator     | Display:12, Radio:17, Input:1, GPS:4, Connectivity/Other:1           |
| seeed-xiao-s3                    | Seeed Xiao ESP32-S3           | 81       | SEEED_XIAO_S3                | variants/esp32s3/seeed_xiao_s3                | Radio:16, Input:1, GPS:6, Power:3                                    |
| station-g2                       | Station G2                    | 31       | STATION_G2                   | variants/esp32s3/station-g2                   | Display:1, Radio:14, Input:1, GPS:2, Power:4                         |
| t-beam-1w                        | LILYGO T-Beam 1W              | 122      | TBEAM_1_WATT                 | variants/esp32s3/t-beam-1w                    | Display:3, Radio:19, Input:2, GPS:6, Power:5                         |
| t-deck                           | LILYGO T-Deck                 | 50       | T_DECK                       | variants/esp32s3/t-deck                       | Display:11, Radio:17, Input:3, GPS:3, Power:5, Connectivity/Other:2  |
| t-deck-pro                       | LILYGO T-Deck Pro             | 102      | T_DECK_PRO                   | variants/esp32s3/t-deck-pro                   | Display:6, Radio:18, Input:2, GPS:7, Power:5, Connectivity/Other:1   |
| t-deck-tft                       |                               |          |                              | variants/esp32s3/t-deck                       | Display:11, Radio:17, Input:3, GPS:3, Power:5, Connectivity/Other:2  |
| t-eth-elite                      |                               |          |                              | variants/esp32s3/t-eth-elite                  | Display:1, Radio:34, Input:1, GPS:5, Connectivity/Other:1            |
| t-watch-s3                       | LILYGO T-Watch S3             | 51       | T_WATCH_S3                   | variants/esp32s3/t-watch-s3                   | Display:12, Radio:17, Input:1, GPS:5, Power:3, Connectivity/Other:3  |
| t5s3_epaper_inkhud               |                               |          |                              | variants/esp32s3/t5s3_epaper                  | Radio:21, Input:3, GPS:2, Power:5, Connectivity/Other:3              |
| tbeam-s3-core                    | LILYGO T-Beam Supreme         | 12       | LILYGO_TBEAM_S3_CORE         | variants/esp32s3/tbeam-s3-core                | Display:1, Radio:25, Input:1, GPS:4, Power:1, Connectivity/Other:2   |
| thinknode_m2                     | ThinkNode M2                  | 90       | THINKNODE_M2                 | variants/esp32s3/ELECROW-ThinkNode-M2         | Display:2, Radio:14, Input:2, GPS:1, Power:6                         |
| thinknode_m5                     | ThinkNode M5                  | 107      | THINKNODE_M5                 | variants/esp32s3/ELECROW-ThinkNode-M5         | Display:6, Radio:14, Input:2, GPS:8, Power:4, Connectivity/Other:1   |
| tlora-pager                      | LILYGO T-LoRa Pager           | 103      | T_LORA_PAGER                 | variants/esp32s3/tlora-pager                  | Display:10, Radio:30, Input:6, GPS:5, Power:5, Connectivity/Other:17 |
| tlora-pager-tft                  |                               |          |                              | variants/esp32s3/tlora-pager                  | Display:10, Radio:30, Input:6, GPS:5, Power:5, Connectivity/Other:17 |
| tlora-t3s3-epaper                | LILYGO T-LoRa T3-S3 E-Ink     | 16       | TLORA_T3_S3                  | variants/esp32s3/tlora_t3s3_epaper            | Display:6, Radio:28, Input:1, GPS:3, Power:3                         |
| tlora-t3s3-epaper-inkhud         |                               |          |                              | variants/esp32s3/tlora_t3s3_epaper            | Display:6, Radio:28, Input:1, GPS:3, Power:3                         |
| tlora-t3s3-v1                    | LILYGO T-LoRa T3-S3           | 16       | TLORA_T3_S3                  | variants/esp32s3/tlora_t3s3_v1                | Display:1, Radio:36, Input:1, Power:3                                |
| unphone                          | unPhone                       | 59       | UNPHONE                      | variants/esp32s3/unphone                      | Display:9, Radio:9, Input:3, GPS:1, Power:2, Connectivity/Other:1    |
| unphone-tft                      |                               |          |                              | variants/esp32s3/unphone                      | Display:9, Radio:9, Input:3, GPS:1, Power:2, Connectivity/Other:1    |

### esp32s2

| Environment    | Display Name | HW Model | HW Slug | Variant Dir                     | Common Categories                      |
| -------------- | ------------ | -------- | ------- | ------------------------------- | -------------------------------------- |
| nugget-s2-lora |              |          |         | variants/esp32s2/nugget_s2_lora | Radio:9, Input:1, Connectivity/Other:1 |

### native

| Environment      | Display Name | HW Model | HW Slug | Variant Dir                         | Common Categories |
| ---------------- | ------------ | -------- | ------- | ----------------------------------- | ----------------- |
| buildroot        |              |          |         | variants/native/portduino-buildroot | Display:2, GPS:1  |
| coverage         |              |          |         | variants/native/portduino           | Display:2, GPS:1  |
| native           |              |          |         | variants/native/portduino           | Display:2, GPS:1  |
| native-fb        |              |          |         | variants/native/portduino           | Display:2, GPS:1  |
| native-tft       |              |          |         | variants/native/portduino           | Display:2, GPS:1  |
| native-tft-debug |              |          |         | variants/native/portduino           | Display:2, GPS:1  |

### nrf52840

| Environment                      | Display Name               | HW Model | HW Slug                   | Variant Dir                                    | Common Categories                                         |
| -------------------------------- | -------------------------- | -------- | ------------------------- | ---------------------------------------------- | --------------------------------------------------------- |
| ME25LS01-4Y10TD                  |                            |          |                           | variants/nrf52840/ME25LS01-4Y10TD              | Radio:8, Input:1, GPS:8, Power:4                          |
| ME25LS01-4Y10TD_e-ink            |                            |          |                           | variants/nrf52840/ME25LS01-4Y10TD_e-ink        | Display:6, Radio:8, Input:1, GPS:8, Power:4               |
| TWC_mesh_v4                      |                            |          |                           | variants/nrf52840/TWC_mesh_v4                  | Display:1, Radio:7, GPS:4, Power:5                        |
| canaryone                        | Canary One                 | 29       | CANARYONE                 | variants/nrf52840/canaryone                    | Radio:7, GPS:8, Power:5                                   |
| feather_diy                      |                            |          |                           | variants/nrf52840/feather_diy                  | Radio:17, Input:1                                         |
| gat562_mesh_trial_tracker        |                            |          |                           | variants/nrf52840/gat562_mesh_trial_tracker    | Display:2, Radio:8, GPS:4, Power:5                        |
| heltec-mesh-node-t096            | Heltec Mesh Node 096       | 127      | HELTEC_MESH_NODE_T096     | variants/nrf52840/heltec_mesh_node_t096        | Display:8, Radio:11, GPS:10, Power:9                      |
| heltec-mesh-node-t114            | Heltec Mesh Node T114      | 69       | HELTEC_MESH_NODE_T114     | variants/nrf52840/heltec_mesh_node_t114        | Display:7, Radio:8, GPS:7, Power:8, Connectivity/Other:1  |
| heltec-mesh-node-t114-inkhud     |                            |          |                           | variants/nrf52840/heltec_mesh_node_t114-inkhud | Display:6, Radio:8, GPS:7, Power:7, Connectivity/Other:1  |
| heltec-mesh-pocket-10000         | Heltec Mesh Pocket         | 94       | HELTEC_MESH_POCKET        | variants/nrf52840/heltec_mesh_pocket           | Display:6, Radio:8, GPS:1, Power:7                        |
| heltec-mesh-pocket-10000-inkhud  | Heltec Mesh Pocket         | 94       | HELTEC_MESH_POCKET        | variants/nrf52840/heltec_mesh_pocket           | Display:6, Radio:8, GPS:1, Power:7                        |
| heltec-mesh-pocket-5000          | Heltec Mesh Pocket         | 94       | HELTEC_MESH_POCKET        | variants/nrf52840/heltec_mesh_pocket           | Display:6, Radio:8, GPS:1, Power:7                        |
| heltec-mesh-pocket-5000-inkhud   | Heltec Mesh Pocket         | 94       | HELTEC_MESH_POCKET        | variants/nrf52840/heltec_mesh_pocket           | Display:6, Radio:8, GPS:1, Power:7                        |
| heltec-mesh-solar                | Heltec MeshSolar           | 108      | HELTEC_MESH_SOLAR         | variants/nrf52840/heltec_mesh_solar            | Radio:8, GPS:6                                            |
| heltec-mesh-solar-eink           |                            |          |                           | variants/nrf52840/heltec_mesh_solar            | Radio:8, GPS:6                                            |
| heltec-mesh-solar-inkhud         |                            |          |                           | variants/nrf52840/heltec_mesh_solar            | Radio:8, GPS:6                                            |
| heltec-mesh-solar-oled           |                            |          |                           | variants/nrf52840/heltec_mesh_solar            | Radio:8, GPS:6                                            |
| heltec-mesh-solar-tft            |                            |          |                           | variants/nrf52840/heltec_mesh_solar            | Radio:8, GPS:6                                            |
| makerpython_nrf52840_sx1280_eink |                            |          |                           | variants/nrf52840/MakePython_nRF52840_eink     | Display:6, Radio:6, GPS:4, Power:5                        |
| makerpython_nrf52840_sx1280_oled |                            |          |                           | variants/nrf52840/MakePython_nRF52840_oled     | Radio:6, GPS:4, Power:5                                   |
| meshlink                         |                            |          |                           | variants/nrf52840/meshlink                     | Display:6, Radio:7, Input:1, GPS:5, Power:5               |
| meshlink_eink                    |                            |          |                           | variants/nrf52840/meshlink                     | Display:6, Radio:7, Input:1, GPS:5, Power:5               |
| meshtiny                         |                            |          |                           | variants/nrf52840/meshtiny                     | Display:2, Radio:8, Input:5, Power:5                      |
| minimesh_lite                    |                            |          |                           | variants/nrf52840/dls_Minimesh_Lite            | Radio:15, Input:1, GPS:4, Power:6                         |
| monteops_hw1                     |                            |          |                           | variants/nrf52840/monteops_hw1                 | Radio:8, GPS:3, Power:5, Connectivity/Other:1             |
| ms24sf1                          |                            |          |                           | variants/nrf52840/MS24SF1                      | Radio:8, Input:1, GPS:8, Power:4                          |
| muzi-base                        | muzi BASE                  | 93       | MUZI_BASE                 | variants/nrf52840/muzi_base                    | Display:3, Radio:19, Input:1, GPS:4, Power:5              |
| nano-g2-ultra                    | Nano G2 Ultra              | 18       | NANO_G2_ULTRA             | variants/nrf52840/nano-g2-ultra                | Display:1, Radio:7, GPS:4, Power:6, Connectivity/Other:1  |
| nrf52_promicro_diy_tcxo          | NRF52 Pro-micro DIY        | 63       | NRF52_PROMICRO_DIY        | variants/nrf52840/diy/nrf52_promicro_diy_tcxo  | Display:4, Radio:29, Input:1, GPS:4, Power:6              |
| pca10059_diy_eink                |                            |          |                           | variants/nrf52840/Dongle_nRF52840-pca10059-v1  | Display:7, Radio:8, GPS:4, Power:5                        |
| r1-neo                           | muzi R1 Neo                | 101      | MUZI_R1_NEO               | variants/nrf52840/r1-neo                       | Display:1, Radio:8, GPS:4, Power:5                        |
| rak2560                          | RAK WisMesh Repeater       | 22       | WISMESH_HUB               | variants/nrf52840/rak2560                      | Display:6, Radio:8, GPS:2, Power:5                        |
| rak3401-1watt                    | RAK3401 1W                 | 117      | RAK3401                   | variants/nrf52840/rak3401_1watt                | Display:6, Radio:12, GPS:3, Power:5                       |
| rak4631                          | RAK WisBlock 4631          | 9        | RAK4631                   | variants/nrf52840/rak4631                      | Display:6, Radio:8, GPS:3, Power:7, Connectivity/Other:1  |
| rak4631_dbg                      |                            |          |                           | variants/nrf52840/rak4631                      | Display:6, Radio:8, GPS:3, Power:7, Connectivity/Other:1  |
| rak4631_eink                     |                            |          |                           | variants/nrf52840/rak4631_epaper               | Display:6, Radio:8, GPS:4, Power:5                        |
| rak4631_eink_onrxtx              |                            |          |                           | variants/nrf52840/rak4631_epaper_onrxtx        | Display:6, Radio:8, Power:1                               |
| rak4631_eth_gw                   |                            |          |                           | variants/nrf52840/rak4631_eth_gw               | Display:6, Radio:8, GPS:3, Power:5, Connectivity/Other:1  |
| rak4631_eth_gw_dbg               |                            |          |                           | variants/nrf52840/rak4631_eth_gw               | Display:6, Radio:8, GPS:3, Power:5, Connectivity/Other:1  |
| rak4631_nomadstar_meteor_pro     | NomadStar Meteor Pro       | 96       | NOMADSTAR_METEOR_PRO      | variants/nrf52840/rak4631_nomadstar_meteor_pro | Display:6, Radio:8, GPS:3, Power:5, Connectivity/Other:1  |
| rak4631_nomadstar_meteor_pro_dbg |                            |          |                           | variants/nrf52840/rak4631_nomadstar_meteor_pro | Display:6, Radio:8, GPS:3, Power:5, Connectivity/Other:1  |
| rak_wismeshtag                   | RAK WisMesh Tag            | 105      | WISMESH_TAG               | variants/nrf52840/rak_wismeshtag               | Display:7, Radio:8, GPS:4, Power:5                        |
| rak_wismeshtap                   | RAK WisMesh Tap            | 84       | WISMESH_TAP               | variants/nrf52840/rak_wismeshtap               | Display:21, Radio:8, GPS:3, Power:7, Connectivity/Other:1 |
| seeed_solar_node                 | Seeed SenseCAP Solar Node  | 95       | SEEED_SOLAR_NODE          | variants/nrf52840/seeed_solar_node             | Radio:9, Input:2, GPS:8, Power:3                          |
| seeed_wio_tracker_L1             | Seeed Wio Tracker L1       | 99       | SEEED_WIO_TRACKER_L1      | variants/nrf52840/seeed_wio_tracker_L1         | Display:2, Radio:9, Input:1, GPS:7, Power:3               |
| seeed_wio_tracker_L1_eink        | Seeed Wio Tracker L1 E-Ink | 100      | SEEED_WIO_TRACKER_L1_EINK | variants/nrf52840/seeed_wio_tracker_L1_eink    | Display:7, Radio:9, Input:1, GPS:7, Power:3               |
| seeed_wio_tracker_L1_eink-inkhud |                            |          |                           | variants/nrf52840/seeed_wio_tracker_L1_eink    | Display:7, Radio:9, Input:1, GPS:7, Power:3               |
| seeed_xiao_nrf52840_kit          | Seeed Xiao NRF52840 Kit    | 88       | XIAO_NRF52_KIT            | variants/nrf52840/seeed_xiao_nrf52840_kit      | Radio:19, Input:2, GPS:9, Power:6                         |
| seeed_xiao_nrf52840_kit_i2c      |                            |          |                           | variants/nrf52840/seeed_xiao_nrf52840_kit      | Radio:19, Input:2, GPS:9, Power:6                         |
| t-echo                           | LILYGO T-Echo              | 7        | T_ECHO                    | variants/nrf52840/t-echo                       | Display:7, Radio:8, GPS:7, Power:6, Connectivity/Other:1  |
| t-echo-inkhud                    |                            |          |                           | variants/nrf52840/t-echo                       | Display:7, Radio:8, GPS:7, Power:6, Connectivity/Other:1  |
| t-echo-lite                      | LILYGO T-Echo Lite         | 109      | T_ECHO_LITE               | variants/nrf52840/t-echo-lite                  | Display:6, Radio:9, GPS:10, Power:8                       |
| t-echo-plus                      |                            |          |                           | variants/nrf52840/t-echo-plus                  | Display:8, Radio:8, GPS:7, Power:6                        |
| thinknode_m1                     | ThinkNode M1               | 89       | THINKNODE_M1              | variants/nrf52840/ELECROW-ThinkNode-M1         | Display:7, Radio:8, Input:1, GPS:8, Power:8               |
| thinknode_m1-inkhud              |                            |          |                           | variants/nrf52840/ELECROW-ThinkNode-M1         | Display:7, Radio:8, Input:1, GPS:8, Power:8               |
| thinknode_m3                     | Elecrow ThinkNode M3       | 115      | THINKNODE_M3              | variants/nrf52840/ELECROW-ThinkNode-M3         | Radio:1, Input:2, GPS:8, Power:7, Connectivity/Other:1    |
| thinknode_m4                     |                            |          |                           | variants/nrf52840/ELECROW-ThinkNode-M4         | Radio:8, GPS:12, Power:7                                  |
| thinknode_m6                     | ThinkNode M6               | 120      | THINKNODE_M6              | variants/nrf52840/ELECROW-ThinkNode-M6         | Radio:7, GPS:9, Power:9, Connectivity/Other:1             |
| tracker-t1000-e                  | Seeed SenseCAP T1000-E     | 71       | TRACKER_T1000_E           | variants/nrf52840/tracker-t1000-e              | Display:1, Radio:8, Input:1, GPS:13, Power:5              |
| wio-sdk-wm1110                   |                            |          |                           | variants/nrf52840/wio-sdk-wm1110               | Radio:8, Input:1                                          |
| wio-t1000-s                      |                            |          |                           | variants/nrf52840/wio-t1000-s                  | Radio:8, Input:1, GPS:12, Power:4                         |
| wio-tracker-wm1110               | Seeed Wio WM1110 Tracker   | 21       | WIO_WM1110                | variants/nrf52840/wio-tracker-wm1110           | Radio:8, Input:1, GPS:3                                   |

### rp2040

| Environment          | Display Name        | HW Model | HW Slug     | Variant Dir                          | Common Categories                                |
| -------------------- | ------------------- | -------- | ----------- | ------------------------------------ | ------------------------------------------------ |
| catsniffer           |                     |          |             | variants/rp2040/ec_catsniffer        | Display:1, Radio:17, GPS:1                       |
| challenger_2040_lora |                     |          |             | variants/rp2040/challenger_2040_lora | Radio:17, Input:1, Power:1                       |
| feather_rp2040_rfm95 |                     |          |             | variants/rp2040/feather_rp2040_rfm95 | Radio:17, Input:1, Power:1                       |
| nibble-rp2040        |                     |          |             | variants/rp2040/nibble_rp2040        | Radio:9, Input:1                                 |
| pico                 | Raspberry Pi Pico   | 47       | RPI_PICO    | variants/rp2040/rpipico              | Radio:16, Input:1, Power:4                       |
| pico_slowclock       |                     |          |             | variants/rp2040/rpipico-slowclock    | Display:2, Radio:16, Input:1, GPS:5, Power:4     |
| picow                | Raspberry Pi Pico W | 47       | RPI_PICO    | variants/rp2040/rpipicow             | Radio:16, Input:1, Power:4, Connectivity/Other:1 |
| rak11310             | RAK WisBlock 11310  | 26       | RAK11310    | variants/rp2040/rak11310             | Radio:17, Input:1, Power:3, Connectivity/Other:1 |
| rp2040-lora          | RP2040 LoRa         | 30       | RP2040_LORA | variants/rp2040/rp2040-lora          | Radio:18, Input:1, Power:1                       |
| senselora_rp2040     |                     |          |             | variants/rp2040/senselora_rp2040     | Display:1, Radio:9, Input:1, Power:1             |

### rp2350

| Environment | Display Name | HW Model | HW Slug | Variant Dir               | Common Categories                                |
| ----------- | ------------ | -------- | ------- | ------------------------- | ------------------------------------------------ |
| pico2       |              |          |         | variants/rp2350/rpipico2  | Radio:16, Input:1, Power:4                       |
| pico2w      |              |          |         | variants/rp2350/rpipico2w | Radio:16, Input:1, Power:4, Connectivity/Other:1 |

### stm32

| Environment     | Display Name | HW Model | HW Slug | Variant Dir                    | Common Categories                  |
| --------------- | ------------ | -------- | ------- | ------------------------------ | ---------------------------------- |
| CDEBYTE_E77-MBL |              |          |         | variants/stm32/CDEBYTE_E77-MBL | Display:1                          |
| milesight_gs301 |              |          |         | variants/stm32/milesight_gs301 | Display:1, Radio:1, Input:1        |
| rak3172         |              |          |         | variants/stm32/rak3172         | Display:1                          |
| russell         |              |          |         | variants/stm32/russell         | Display:1, Radio:1, Input:1, GPS:4 |
| wio-e5          |              |          |         | variants/stm32/wio-e5          | Display:1                          |

## Representative Board Examples

### diy: `9m2ibr_aprs_lora_tracker`

- Variant directory: `variants/esp32/diy/9m2ibr_aprs_lora_tracker`
- Input examples:
  - `BUTTON_PIN` = `15`
- Radio examples:
  - `LORA_SCK` = `18`
  - `LORA_MISO` = `19`
  - `LORA_MOSI` = `23`
  - `LORA_CS` = `5`
  - `LORA_DIO0` = `26`
  - `LORA_RESET` = `27`
  - `LORA_DIO1` = `12`
  - `LORA_DIO2` = `RADIOLIB_NC`
- GPS examples:
  - `GPS_RX_PIN` = `16`
  - `GPS_TX_PIN` = `17`
- Display examples:
  - `HAS_SCREEN` = `1`
- I2C/SPI examples:
  - `I2C_SDA` = `21`
  - `I2C_SCL` = `22`
- Power examples:
  - `BATTERY_PIN` = `35`
  - `ADC_MULTIPLIER` = `2.01`
  - `ADC_CHANNEL` = `ADC1_GPIO35_CHANNEL`
  - `BATTERY_SENSE_RESOLUTION_BITS` = `ADC_RESOLUTION`

### esp32: `betafpv_2400_tx_micro`

- Variant directory: `variants/esp32/betafpv_2400_tx_micro`
- Input examples:
  - `BUTTON_PIN` = `25`
- Radio examples:
  - `LORA_SCK` = `18`
  - `LORA_MISO` = `19`
  - `LORA_MOSI` = `23`
  - `LORA_CS` = `5`
  - `RF95_FAN_EN` = `17`
  - `USE_SX1280` = `1`
  - `LORA_RESET` = `14`
  - `SX128X_CS` = `5`
- I2C/SPI examples:
  - `I2C_SDA` = `22`
  - `I2C_SCL` = `32`
- Connectivity/Other examples:
  - `HAS_NEOPIXEL` = `1`

### esp32-c3: `ai-c3`

- Variant directory: `variants/esp32c3/ai-c3`
- Input examples:
  - `BUTTON_PIN` = `9`
- Radio examples:
  - `USE_RF95` = `1`
  - `LORA_SCK` = `4`
  - `LORA_MISO` = `5`
  - `LORA_MOSI` = `6`
  - `LORA_CS` = `7`
  - `LORA_DIO0` = `10`
  - `LORA_DIO1` = `3`
  - `LORA_RESET` = `2`
- GPS examples:
  - `HAS_GPS` = `0`
- I2C/SPI examples:
  - `I2C_SDA` = `SDA`
  - `I2C_SCL` = `SCL`

### esp32-c6: `m5stack-unitc6l`

- Variant directory: `variants/esp32c6/m5stack_unitc6l`
- `custom_meshtastic_hw_model`: `111`
- `custom_meshtastic_hw_model_slug`: `M5STACK_C6L`
- `custom_meshtastic_architecture`: `esp32-c6`
- `custom_meshtastic_actively_supported`: `true`
- `custom_meshtastic_support_level`: `1`
- `custom_meshtastic_display_name`: `M5Stack Unit C6L`
- `custom_meshtastic_images`: `m5_c6l.svg`
- `custom_meshtastic_tags`: `M5Stack`
- Radio examples:
  - `USE_SX1262` = `1`
  - `LORA_MISO` = `22`
  - `LORA_SCK` = `20`
  - `LORA_MOSI` = `21`
  - `LORA_CS` = `23`
  - `LORA_RESET` = `RADIOLIB_NC`
  - `LORA_DIO1` = `7`
  - `LORA_BUSY` = `19`
- GPS examples:
  - `HAS_GPS` = `1`
  - `GPS_RX_PIN` = `4`
  - `GPS_TX_PIN` = `5`
- Display examples:
  - `SCREEN_TRANSITION_FRAMERATE` = `10`
- I2C/SPI examples:
  - `I2C_SDA` = `10`
  - `I2C_SCL` = `8`
- Connectivity/Other examples:
  - `HAS_NEOPIXEL` = `1`

### esp32-s3: `CDEBYTE_EoRa-Hub`

- Variant directory: `variants/esp32s3/CDEBYTE_EoRa-Hub`
- Input examples:
  - `BUTTON_PIN` = `0`
- Radio examples:
  - `USE_LR1121` = `1`
  - `LORA_SCK` = `9`
  - `LORA_MOSI` = `10`
  - `LORA_MISO` = `11`
  - `LORA_RESET` = `12`
  - `LORA_CS` = `8`
  - `LORA_DIO9` = `13`
  - `LR1121_IRQ_PIN` = `14`
- Display examples:
  - `HAS_SCREEN` = `1`
  - `USE_SSD1306` = `1`
- I2C/SPI examples:
  - `I2C_SCL` = `17`
  - `I2C_SDA` = `18`
  - `I2C_SCL1` = `21`
  - `I2C_SDA1` = `10`
- Power examples:
  - `BATTERY_PIN` = `1`
  - `ADC_CHANNEL` = `ADC1_GPIO1_CHANNEL`
  - `ADC_MULTIPLIER` = `103.0`
  - `ADC_ATTENUATION` = `ADC_ATTEN_DB_0`
  - `ADC_CTRL` = `37`
  - `ADC_CTRL_ENABLED` = `LOW`

### esp32s2: `nugget-s2-lora`

- Variant directory: `variants/esp32s2/nugget_s2_lora`
- Input examples:
  - `BUTTON_PIN` = `0`
- Radio examples:
  - `USE_RF95` = `1`
  - `LORA_SCK` = `6`
  - `LORA_MISO` = `8`
  - `LORA_MOSI` = `10`
  - `LORA_CS` = `13`
  - `LORA_DIO0` = `16`
  - `LORA_RESET` = `5`
  - `LORA_DIO1` = `RADIOLIB_NC`
- I2C/SPI examples:
  - `I2C_SDA` = `34`
  - `I2C_SCL` = `36`
- Connectivity/Other examples:
  - `HAS_NEOPIXEL` = `1`

### native: `buildroot`

- Variant directory: `variants/native/portduino-buildroot`
- GPS examples:
  - `HAS_GPS` = `1`
- Display examples:
  - `HAS_SCREEN` = `1`
  - `USE_TFTDISPLAY` = `1`

### nrf52840: `ME25LS01-4Y10TD`

- Variant directory: `variants/nrf52840/ME25LS01-4Y10TD`
- Input examples:
  - `BUTTON_PIN` = `(0 + 27)`
- Radio examples:
  - `LORA_RESET` = `(32 + 11)`
  - `LORA_DIO1` = `(32 + 12)`
  - `LORA_DIO2` = `(32 + 10)`
  - `LORA_SCK` = `PIN_SPI_SCK`
  - `LORA_MISO` = `PIN_SPI_MISO`
  - `LORA_MOSI` = `PIN_SPI_MOSI`
  - `LORA_CS` = `PIN_SPI_NSS`
  - `USE_LR1110` = `1`
- GPS examples:
  - `HAS_GPS` = `0`
  - `PIN_GPS_EN` = `-1`
  - `GPS_EN_ACTIVE` = `HIGH`
  - `PIN_GPS_RESET` = `-1`
  - `GPS_VRTC_EN` = `-1`
  - `GPS_SLEEP_INT` = `-1`
  - `GPS_RTC_INT` = `-1`
  - `GPS_RESETB_OUT` = `-1`
- I2C/SPI examples:
  - `WIRE_INTERFACES_COUNT` = `1`
  - `SPI_INTERFACES_COUNT` = `1`
  - `PIN_SPI_MISO` = `(0 + 29)`
  - `PIN_SPI_MOSI` = `(0 + 2)`
  - `PIN_SPI_SCK` = `(32 + 15)`
  - `PIN_SPI_NSS` = `(32 + 13)`
- Power examples:
  - `BATTERY_PIN` = `-1`
  - `ADC_MULTIPLIER` = `(2.0F)`
  - `ADC_RESOLUTION` = `14`
  - `BATTERY_SENSE_RESOLUTION_BITS` = `12`

### rp2040: `catsniffer`

- Variant directory: `variants/rp2040/ec_catsniffer`
- Radio examples:
  - `USE_SX1262` = `1`
  - `LORA_SCK` = `18`
  - `LORA_MISO` = `16`
  - `LORA_MOSI` = `19`
  - `LORA_CS` = `17`
  - `LORA_DIO0` = `5`
  - `LORA_RESET` = `24`
  - `LORA_DIO1` = `4`
- GPS examples:
  - `HAS_GPS` = `0`
- Display examples:
  - `HAS_SCREEN` = `0`

### rp2350: `pico2`

- Variant directory: `variants/rp2350/rpipico2`
- Input examples:
  - `BUTTON_PIN` = `17`
- Radio examples:
  - `USE_SX1262` = `1`
  - `LORA_SCK` = `10`
  - `LORA_MISO` = `12`
  - `LORA_MOSI` = `11`
  - `LORA_CS` = `3`
  - `LORA_DIO0` = `RADIOLIB_NC`
  - `LORA_RESET` = `15`
  - `LORA_DIO1` = `20`
- Power examples:
  - `EXT_NOTIFY_OUT` = `22`
  - `BATTERY_PIN` = `26`
  - `ADC_MULTIPLIER` = `3.1`
  - `BATTERY_SENSE_RESOLUTION_BITS` = `ADC_RESOLUTION`

### stm32: `CDEBYTE_E77-MBL`

- Variant directory: `variants/stm32/CDEBYTE_E77-MBL`
- Display examples:
  - `USE_STM32WLx` = `1`

## Intake Guidance For New Hardware

When using this context to add a new board, collect these inputs before generating files:

- PlatformIO environment name
- `custom_meshtastic_hw_model` and `custom_meshtastic_hw_model_slug`
- Display name and architecture
- Whether the board is actively supported and its support level
- Partition scheme, DFU requirement, and image/tag metadata if applicable
- Radio chip family and complete radio pin group
- Input/button/rotary/keyboard pins
- Display interface pins and driver-related macros
- GPS, power-management, I2C, SPI, storage, and auxiliary peripheral definitions
- Any board-specific initialization that requires `variant.cpp` or extra variant hooks

## Inherited Defaults Note

Some architecture families rely on BSP (Board Support Package) or base-environment defaults rather than declaring every pin or capability macro explicitly in `variant.h`. When adding a new board for one of these families, check the relevant BSP headers before assuming a missing define means a feature is absent.

Directories scanned that had no `variant.h` (relying entirely on BSP/base-environment): diy: 4, esp32: 3, esp32s3: 1

### nrf52840

- VARIANT_MCK — nRF52 BSP clock constant (e.g., 64000000ul); always inherited from BSP unless overridden.
- USE_LFXO — low-frequency crystal oscillator selection; declared locally only when the board uses LFXO rather than the RC oscillator.
- PIN*SPI*_ / PIN*SPI1*_ — SPI bus pin numbers come from the BSP variant table; boards override only when the LoRa radio or display uses non-default SPI routing.
- WIRE_INTERFACES_COUNT / SPI_INTERFACES_COUNT — bus count comes from BSP; explicitly set only when the board deviates.
- LED_BLUE / PIN_LED1 / PINS_COUNT / NUM_DIGITAL_PINS — standard BSP pin-table entries inherited from the nRF52 Arduino core.

### rp2040

- PIN*SPI*\* — primary SPI pins come from the RP2040 Arduino BSP; most boards declare them explicitly, but the defaults align with the Pico pin assignments.
- NUM_DIGITAL_PINS / NUM_ANALOG_INPUTS — Arduino BSP counts; rarely overridden locally.

### stm32

- Radio and pin assignments for STM32WL targets are largely internal to the WL SoC and declared via STM32 HAL/BSP headers; variant.h files are minimal.
- USE_STM32WLx is typically the only explicit define; all other radio config comes from the BSP.

### native

- The native/Portduino target uses runtime configuration rather than compile-time pin defines; variant.h only sets display and GPS stubs.

## Cautions

- Some boards rely on architecture defaults rather than declaring every field locally.
- Some board families expose multiple environments or display variants that share one hardware model.
- Source materials such as schematics still need human verification before new pin mappings are trusted.
- This document is a starting context artifact, not proof that a new board definition is safe to merge.
