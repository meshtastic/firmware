# XIAO nrf52840/nrf52840 Sense + Ebyte E22-900M30S

_A step-by-step guide for macOS and Linux._

## Introduction

This guide will walk you through everything needed to get the XIAO nrf52840 (or XIAO nrf52840 Sense) running Meshtastic using an Ebyte E22-900M30S LoRa module. The combination of the E22 with an nRF52840 MCU is desirable because it allows for both very low idle (Rx) power draw _and_ high transmit power.

The XIAO nrf52840 is a small but surprisingly well-appointed nRF52840 board, with enough GPIO for most Meshtastic applications and a built-in LiPo charger.

The E22, on the other hand, is a famously inscrutable and mysterious beast. It is one of the more readily available LoRa modules capable of transmitting at 30 dBm, and includes an LNA to boost its Rx sensitivity a few dB beyond that of the SX1262.

However, its documentation is relatively sparse overall, and seems to merely hint at (or completely omit) several key details regarding its functionality. Thus, much of what follows is a synthesis of my observations and inferences over the course of many hours of trial and error.

### Acknowledgement and Friendly Disclaimer

Huge thanks to those in the community who have forged the way with the E22, without whose hard work none of this would have been possible! (thebentern, riddick, rainer_vie, beegee-tokyo, geeksville, caveman99, Der_Bear, PlumRugOfDoom, BigCorvus, and many others.)

Please take the conclusions here as a tentative work in progress, representing my current (and fairly limited) understanding of the E22 when paired with this particular MCU. It is my hope that this guide will be helpful to others who are interested in trying a DIY Meshtastic build, and also be subject to revision by folks with more experience and better test equipment.

### Obligatory Liability Disclaimer

This guide and all associated content is for informational purposes only. The information presented is intended for consumption only by persons having appropriate technical skill and judgement, to be used entirely at their own discretion and risk. The authors of this guide in no way provide any warranty, express or implied, toward the content herein, nor its correctness, safety, or suitability to any particular purpose. By following the instructions in this guide in part or in full, you assume all responsibility for all potential risks, including but not limited to fire, property damage, bodily injury, and death.

## 1. Wire the board

Connecting the E22 to the XIAO nrf52840 is straightforward, but there are a few gotchas to be mindful of.

### On the XIAO nrf52840

- Pins D4 and D5 are currently mapped to `PIN_WIRE_SDA` and `PIN_WIRE_SCL`, respectively. If you are not using I²C and would like to free up pins D4 and D5 for use as GPIO, `PIN_WIRE_SDA` and `PIN_WIRE_SCL` can be reassigned to any two other unused pin numbers.
- Pins D6 and D7 were originally mapped to the TX and RX pins for serial interface 1 (`PIN_SERIAL1_RX` and `PIN_SERIAL1_TX`) but are currently set to -1 in `variant.h`. If you need to expose a serial interface, you can restore these pins and move e.g. `SX126X_RXEN` to pin 4 or 5 (the opposite should work too).

### On the E22

- There are two options for the E22's `TXEN` pin:
  1. It can be connected to the MCU on the pin defined as `SX126X_TXEN` in `variant.h`. In this configuration, the MCU will control Tx/Rx switching "manually". As long as `SX126X_TXEN` and `SX126X_RXEN` are both defined in `variant.h` (and neither is set to `RADIOLIB_NC`), `SX126xInterface.cpp` will initialize the E22 correctly for this mode.
  2. Alternately, it can be connected to the E22's `DIO2` pin only, with neither `TXEN` nor `DIO2` being connected to the MCU. In this configuration, the E22 will control Tx/Rx switching automatically. In `variant.h`, as long as `SX126X_TXEN` is defined as `RADIOLIB_NC`, and `SX126X_RXEN` is defined and connected to the E22's `RXEN` pin, and `E22_TXEN_CONNECTED_TO_DIO2` is defined, `SX126xInterface.cpp` will initialize the E22 correctly for this mode. This configuration frees up a GPIO, and presents no drawbacks that I have found.
- Note that any combination other than the two described above will likely result in unexpected behavior. In my testing, some of these other configurations appeared to "work" at first glance, but every one I tried had at least one of the following flaws: weak Tx power, extremely poor Rx sensitivity, or the E22 overheating because TXEN was never pulled low, causing its PA to stay on indefinitely.
- Along the same lines, it is a good idea to check the E22's temperature frequently by lightly touching the shield. If you feel the shield getting hot (i.e. approaching uncomfortable to touch) near pins 1, 2, and 3, something is probably misconfigured; disconnect both the XIAO nrf52840 and E22 from power and double check wiring and pin mapping.
- Whether you opt to let the E22 control Rx and Tx or handle this manually, **the E22's `RXEN` pin must always be connected to the MCU** on the pin defined as `SX126X_RXEN` in `variant.h`.

#### Note

The default pin mapping in `variant.h` uses "Automatic Tx/Rx switching" mode.

If you wire your board for Manual Tx/Rx Switching Mode, `SX126X_TXEN` must be defined (`#define #define SX126X_TXEN D6`) in `variants/seeed_xiao_nrf52840_kit/variant.h` in the code block following:

```c
#ifdef XIAO_BLE_LEGACY_PINOUT
// Legacy xiao_ble variant pinout for third-party SX126x modules e.g. EBYTE E22
```

### Example Wiring for Automatic Tx/Rx Switching Mode

#### MCU -> E22 Connections

| XIAO nrf52840 pin | variant.h definition | E22 pin   | Notes                                                                                                                |
| :---------------- | :------------------- | :-------- | :------------------------------------------------------------------------------------------------------------------- |
| D0                | SX126X_CS            | 19 (NSS)  |                                                                                                                      |
| D1                | SX126X_DIO1          | 13 (DIO1) |                                                                                                                      |
| D2                | SX126X_BUSY          | 14 (BUSY) |                                                                                                                      |
| D3                | SX126X_RESET         | 15 (NRST) |                                                                                                                      |
| D7                | SX126X_RXEN          | 6 (RXEN)  | These pins must still be connected, and `SX126X_RXEN` defined in `variant.h`, otherwise Rx sensitivity will be poor. |
| D8                | PIN_SPI_SCK          | 18 (SCK)  |                                                                                                                      |
| D9                | PIN_SPI_MISO         | 16 (MISO) |                                                                                                                      |
| D10               | PIN_SPI_MOSI         | 17 (MOSI) |                                                                                                                      |

#### E22 -> E22 Connections

| E22 pin | E22 pin | Notes                                                                     |
| :------ | :------ | :------------------------------------------------------------------------ |
| TXEN    | DIO2    | These must be physically connected for automatic Tx/Rx switching to work. |

#### Note

The schematic (`xiao-ble-e22-schematic.png`) in the `eagle-project` directory uses this wiring.

### Example Wiring for Manual Tx/Rx Switching Mode

#### MCU -> E22 Connections

| XIAO nrf52840 pin | variant.h definition | E22 pin   | Notes |
| :---------------- | :------------------- | :-------- | :---- |
| D0                | SX126X_CS            | 19 (NSS)  |       |
| D1                | SX126X_DIO1          | 13 (DIO1) |       |
| D2                | SX126X_BUSY          | 14 (BUSY) |       |
| D3                | SX126X_RESET         | 15 (NRST) |       |
| D6                | SX126X_TXEN          | 7 (TXEN)  |       |
| D7                | SX126X_RXEN          | 6 (RXEN)  |       |
| D8                | PIN_SPI_SCK          | 18 (SCK)  |       |
| D9                | PIN_SPI_MISO         | 16 (MISO) |       |
| D10               | PIN_SPI_MOSI         | 17 (MOSI) |       |

#### E22 -> E22 connections

_(none)_

## 2. Build Meshtastic

1. Follow the [Building Meshtastic Firmware](https://meshtastic.org/docs/development/firmware/build/) documentation, stop after **Build** → **Step 2**
2. For **Build** → **Step 3**, select `xiao_ble` as your target
3. Adjust source code if you:
   - Wired your board for Manual Tx/Rx Switching Mode: see [Wire the Board](#1-wire-the-board)
   - Used an E22-900M33S module  
     (this step is important to avoid **damaging the power amplifier** in the M33S module and **transmitting power above legal limits**!):
     1. Open `variants/diy/platformio.ini`
     2. Search for `[env:xiao_ble]`
     3. In the line starting with `build_flags` within this section, change `-DEBYTE_E22_900M30S` to `-DEBYTE_E22_900M33S`
4. Follow **Build** → **Step 4** to build the firmware
5. Stop here, because the **PlatformIO: Upload** step does not work for factory-fresh XIAO nrf52840 (the automatic reset to bootloader only works if Meshtastic firmware is already running)
6. The built `firmware.uf2` binary can be found in the folder `.pio/build/xiao_ble/firmware.uf2` (relative to where you cloned the Git repository to), we will need it for [flashing the firmware](#3-flash-the-firmware-to-the-xiao-nrf52840) (manually)

## 3. Flash the Firmware to the XIAO nrf52840

1. Double press the XIAO nrf52840's `reset` button to put it in bootloader mode, and a USB volume named `XIAO SENSE` will appear
2. Copy the `firmware.uf2` file to the `XIAO SENSE` volume (refer to the last step of [Build Meshtastic](#2-build-meshtastic))
3. The XIAO nrf52840's red LED will flash for several seconds as the firmware is copied
4. Once Meshtastic firmware succesfully boots, the:
   1. Green LED will turn on
   2. Red LED will flash several times to indicate flash memory writes during initial settings file creation
   3. Green LED will blink every second once the firmware is running normally
5. If you do not see the above LED patters, proceed to [Troubleshooting](#4-troubleshooting)

## 4. Troubleshooting

- If after flashing Meshtastic, the XIAO is bootlooped, look at the serial output (you can see this by running `meshtastic --noproto` with the device connected to your computer via USB).
  - If you see that the SX1262 init result was -2, this likely indicates a wiring problem; double check your wiring and pin mapping in `variant.h`.
  - If you see an error mentioning tinyFS, this may mean you need to reformat the XIAO's storage:
    1. Open the [Meshtastic web flasher](https://flasher.meshtastic.org/)
    2. Select the **_Seeed XIAO NRF52840 Kit_**
    3. Click the **_trash can icon_** to the right of **_Flash_**
    4. Follow the instructions on the screen
       **Do not flash the Seeed XIAO NRF52840 Kit firmware** if you have wired the LoRa module according to this variant, as the Seeed XIAO NRF52840 Kit uses different wiring for the SX1262 LoRa chip
  - If you don't see any specific error message, but the boot process is stuck or not proceeding as expected, this might also mean there is a conflict in `variant.h`. If you have made any changes to the pin mapping, ensure they do not result in a conflict. If all else fails, try reverting your changes and using the known-good configuration included here.
  - The above might also mean something is wired incorrectly. Try reverting to one of the known-good example wirings in section 4.
- If the E22 gets hot to the touch:
  - The power amplifier is likely running continually. Disconnect it and the XIAO from power immediately, and double check wiring and pin mapping. In my experimentation this occurred in cases where TXEN was inadvertenly high (usually due to a pin mapping conflict).

## 5. Notes

- **Transmit Power**
  - There is a power amplifier after the SX1262's Tx, so the actual Tx power is just over 7 dB greater than the SX1262's set Tx power (the E22-900M30S actually tops out just over 29dB at 5V according to the datasheet)
  - Meshtastic firmware is aware of the gain of the E22-900M30S module, so the Meshtastic clients' Tx power setting reflects the actual output power, i.e. setting 30 dBm in the Meshtastic app programs the E22 module to correctly output 30 dBm, setting 24 dBm will output 24 dBm, etc.
- **Adequate 5V Power Supply to the E22 Module**
  - Have a bypass capacitor from its 5V supply to ground; 100 µF works well
  - Voltage must be between 5V–5.5V, lower supply voltage results in less output power; for example, with a fully charged LiPo at 4.2V, Tx power appears to max out around 26-27 dBm

### Additional Reading

- [S5NC/CDEBYTE_Modules](https://github.com/S5NC/CDEBYTE_Modules) has additional information about EBYTE E22 modules' internal workings, including photographs
- [RadioLib High power Radio Modules Guide](https://github.com/jgromes/RadioLib/wiki/High-power-Radio-Modules-Guide)

## 6. Testing Methodology

During what became a fairly long trial-and-error process, I did a lot of careful testing of Tx power and Rx sensitivity. My methodology in these tests was as follows:

- All tests were conducted between two nodes:
  1. The XIAO nrf52840 + E22 coupled with an [Abracon ARRKP4065-S915A](https://www.digikey.com/en/products/detail/abracon-llc/ARRKP4065-S915A/8593263") ceramic patch antenna
  2. A RAK 5005/4631 coupled with a [Laird MA9-5N](https://www.streakwave.com/laird-technologies-ma9-5n-55dbi-900mhz-mobile-omni-select-mount) antenna via a 4" U.FL to Type N pigtail.
  - No other nodes were powered up onsite or nearby.
- Each node and its antenna was kept in exactly the same position and orientation throughout testing.
- Other environmental factors (e.g. the location and resting position of my body in the room while testing) were controlled as carefully as possible.
- Each test comprised at least five (and often ten) runs, after which the results were averaged.
- All testing was done by sending single-character messages between nodes and observing the received RSSI reported in the message acknowledgement. Messages were sent one by one, waiting for each to be acknowledged or time out before sending the next.
- The E22's Tx power was observed by sending messages from the RAK to the XIAO nrf52840 + E22 and recording the received RSSI.
- The opposite was done to observe the E22's Rx sensitivity: messages were sent from the XIAO nrf52840 + E22 to the RAK, and the received RSSI was recorded.
  While this cannot match the level of accuracy achievable with actual test equipment in a lab setting, it was nonetheless sufficient to demonstrate the (sometimes very large) differences in Tx power and Rx sensitivity between various configurations.
