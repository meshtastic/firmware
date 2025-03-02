#

<p align="center" style="font-size: 28px;">
    Xiao BLE/BLE Sense + Ebyte E22-900M30S
</p>

<p align="center" style="font-size: 20px;">
    A step-by-step guide for macOS and Linux
</p>

## Introduction

This guide will walk you through everything needed to get the Xiao BLE (or BLE Sense) running Meshtastic using an Ebyte E22-900M30S LoRa module. The combination of the E22 with an nRF52840 MCU is desirable because it allows for both very low idle (Rx) power draw <i>and</i> high transmit power. The Xiao BLE is a small but surprisingly well-appointed nRF52840 board, with enough GPIO for most Meshtastic applications and a built-in LiPo charger. The E22, on the other hand, is a famously inscrutable and mysterious beast. It is one of the more readily available LoRa modules capable of transmitting at 30 dBm, and includes an LNA to boost its Rx sensitivity a few dB beyond that of the SX1262. However, its documentation is relatively sparse overall, and seems to merely hint at (or completely omit) several key details regarding its functionality. Thus, much of what follows is a synthesis of my observations and inferences over the course of many hours of trial and error.

<h3>Acknowledgement and friendly disclaimer</h3>

Huge thanks to those in the community who have forged the way with the E22, without whose hard work none of this would have been possible! (thebentern, riddick, rainer_vie, beegee-tokyo, geeksville, caveman99, Der_Bear, PlumRugOfDoom, BigCorvus, and many others.)

<br>

Please take the conclusions here as a tentative work in progress, representing my current (and fairly limited) understanding of the E22 when paired with this particular MCU. It is my hope that this guide will be helpful to others who are interested in trying a DIY Meshtastic build, and also be subject to revision by folks with more experience and better test equipment.

### Obligatory liability disclaimer

This guide and all associated content is for informational purposes only. The information presented is intended for consumption only by persons having appropriate technical skill and judgement, to be used entirely at their own discretion and risk. The authors of this guide in no way provide any warranty, express or implied, toward the content herein, nor its correctness, safety, or suitability to any particular purpose. By following the instructions in this guide in part or in full, you assume all responsibility for all potential risks, including but not limited to fire, property damage, bodily injury, and death.

### Note

These instructions assume you are running macOS or Linux, but it should be relatively easy to translate each command for Windows. (In this case, in step 2 below, each line of `xiao_ble.sh` would also need to be converted to the equivalent Windows CLI command and run individually.)

## 1. Update Bootloader

The first thing you will need to do is update the Xiao BLE's bootloader. The stock bootloader is functionally very similar to the Adafruit nRF52 UF2 bootloader, but apparently not quite enough so to work with Meshtastic out of the box.

1. Connect the Xiao BLE to your computer via USB-C.

2. Install `adafruit-nrfutil` by following the instructions <a href="https://github.com/adafruit/Adafruit_nRF52_nrfutil">here</a>.

3. Open a terminal window and navigate to `firmware/variants/xiao_ble` (where `firmware` is the directory into which you have cloned the <a href="https://github.com/meshtastic/firmware">Meshtastic firmware repo</a>).

4. Run the following command, replacing `/dev/cu.usbmodem2101` with the serial port your Xiao BLE is connected to:

   ```bash
   adafruit-nrfutil --verbose dfu serial --package xiao_nrf52840_ble_bootloader-0.7.0-22-g277a0c8_s140_7.3.0.zip --port /dev/cu.usbmodem2101 -b 115200 --singlebank --touch 1200
   ```

5. If all goes well, the Xiao BLE's red LED should start to pulse slowly, and you should see a new USB storage device called `XIAO-BOOT` appear under `Locations` in Finder.

&nbsp;

## 2. PlatformIO Environment Preparation

Before building Meshtastic for the Xiao BLE + E22, it is necessary to pull in SoftDevice 7.3.0 and its associated linker script (nrf52840_s140_v7.ld) from Seeed Studio's Arduino core. The `xiao_ble.sh` script does this.

1. In your terminal window, run the following command:

   ```bash
   sudo ./xiao_ble.sh
   ```

&nbsp;

## 3. Build Meshtastic

At this point, you should be able to build the firmware successfully.

1. In VS Code, press `Command Shift P` to bring up the command palette.

2. Search for and run the `Developer: Reload Window` command.

3. Bring up the command palette again with `Command Shift P`. Search for and run the `PlatformIO: Pick Project Environment` command.

4. In the list of environments, select `env:xiao_ble`. PlatformIO may update itself for a minute or two, and should let you know once done.

5. Return to the command palette once again (`Command Shift P`). Search for and run the `PlatformIO: Build` command.

6. PlatformIO will build the project. After a few minutes you should see a green `SUCCESS` message.

&nbsp;

## 4. Wire the board

Connecting the E22 to the Xiao BLE is straightforward, but there are a few gotchas to be mindful of.

- <strong>On the Xiao BLE:</strong>

  - Pins D4 and D5 are currently mapped to `PIN_WIRE_SDA` and `PIN_WIRE_SCL`, respectively. If you are not using I²C and would like to free up pins D4 and D5 for use as GPIO, `PIN_WIRE_SDA` and `PIN_WIRE_SCL` can be reassigned to any two other unused pin numbers.

  - Pins D6 and D7 were originally mapped to the TX and RX pins for serial interface 1 (`PIN_SERIAL1_RX` and `PIN_SERIAL1_TX`) but are currently set to -1 in `variant.h`. If you need to expose a serial interface, you can restore these pins and move e.g. `SX126X_RXEN` to pin 4 or 5 (the opposite should work too).

- <strong>On the E22:</strong>

  - There are two options for the E22's `TXEN` pin:

    1. It can be connected to the MCU on the pin defined as `SX126X_TXEN` in `variant.h`. In this configuration, the MCU will control Tx/Rx switching "manually". As long as `SX126X_TXEN` and `SX126X_RXEN` are both defined in `variant.h` (and neither is set to `RADIOLIB_NC`), `SX126xInterface.cpp` will initialize the E22 correctly for this mode.

    2. Alternately, it can be connected to the E22's `DIO2` pin only, with neither `TXEN` nor `DIO2` being connected to the MCU. In this configuration, the E22 will control Tx/Rx switching automatically. In `variant.h`, as long as `SX126X_TXEN` is defined as `RADIOLIB_NC`, and `SX126X_RXEN` is defined and connected to the E22's `RXEN` pin, and `E22_TXEN_CONNECTED_TO_DIO2` is defined, `SX126xInterface.cpp` will initialize the E22 correctly for this mode. This configuration frees up a GPIO, and presents no drawbacks that I have found.

    - Note that any combination other than the two described above will likely result in unexpected behavior. In my testing, some of these other configurations appeared to "work" at first glance, but every one I tried had at least one of the following flaws: weak Tx power, extremely poor Rx sensitivity, or the E22 overheating because TXEN was never pulled low, causing its PA to stay on indefinitely.

    - Along the same lines, it is a good idea to check the E22's temperature frequently by lightly touching the shield. If you feel the shield getting hot (i.e. approaching uncomfortable to touch) near pins 1, 2, and 3, something is probably misconfigured; disconnect both the Xiao BLE and E22 from power and double check wiring and pin mapping.

  - Whether you opt to let the E22 control Rx and Tx or handle this manually, <strong>the E22's `RXEN` pin must always be connected to the MCU</strong> on the pin defined as `SX126X_RXEN` in `variant.h`.

<h3>Note</h3>

The default pin mapping in `variant.h` uses 'automatic Tx/Rx switching' mode. If you wire your board for manual Rx/Tx switching, make sure to update `variant.h` accordingly by commenting/uncommenting the necessary lines in the 'E22 Tx/Rx control options' section.

&nbsp;

---

&nbsp;

<h3>Example wiring for "E22 automatic Tx/Rx switching" mode:</h3>
&nbsp;

<strong>MCU -> E22 connections</strong>

| Xiao BLE pin | variant.h definition | E22 pin   | Notes                                                                                                                |
| :----------- | :------------------- | :-------- | :------------------------------------------------------------------------------------------------------------------- |
| D0           | SX126X_CS            | 19 (NSS)  |                                                                                                                      |
| D1           | SX126X_DIO1          | 13 (DIO1) |                                                                                                                      |
| D2           | SX126X_BUSY          | 14 (BUSY) |                                                                                                                      |
| D3           | SX126X_RESET         | 15 (NRST) |                                                                                                                      |
| D7           | SX126X_RXEN          | 6 (RXEN)  | These pins must still be connected, and `SX126X_RXEN` defined in `variant.h`, otherwise Rx sensitivity will be poor. |
| D8           | PIN_SPI_SCK          | 18 (SCK)  |                                                                                                                      |
| D9           | PIN_SPI_MISO         | 16 (MISO) |                                                                                                                      |
| D10          | PIN_SPI_MOSI         | 17 (MOSI) |                                                                                                                      |

&nbsp;
&nbsp;

<strong>E22 -> E22 connections:</strong>

| E22 pin | E22 pin | Notes                                                                     |
| :------ | :------ | :------------------------------------------------------------------------ |
| TXEN    | DIO2    | These must be physically connected for automatic Tx/Rx switching to work. |

<h3>Note</h3>

The schematic (`xiao-ble-e22-schematic.png`) in the `eagle-project` directory uses this wiring.

&nbsp;

---

&nbsp;

<h3>Example wiring for "Manual Tx/Rx switching" mode:</h3>

<strong>MCU -> E22 connections</strong>

| Xiao BLE pin | variant.h definition | E22 pin   | Notes |
| :----------- | :------------------- | :-------- | :---- |
| D0           | SX126X_CS            | 19 (NSS)  |       |
| D1           | SX126X_DIO1          | 13 (DIO1) |       |
| D2           | SX126X_BUSY          | 14 (BUSY) |       |
| D3           | SX126X_RESET         | 15 (NRST) |       |
| D6           | SX126X_TXEN          | 7 (TXEN)  |       |
| D7           | SX126X_RXEN          | 6 (RXEN)  |       |
| D8           | PIN_SPI_SCK          | 18 (SCK)  |       |
| D9           | PIN_SPI_MISO         | 16 (MISO) |       |
| D10          | PIN_SPI_MOSI         | 17 (MOSI) |       |

<strong>E22 -> E22 connections:</strong> (none)

&nbsp;

## 5. Flash the firmware to the Xiao BLE

1. Double press the Xiao's `reset` button to put it in bootloader mode.
2. In a terminal window, navigate to the Meshtastic firmware repo's root directory, and from there to `.pio/build/xiao_ble`.
3. Convert the generated `.hex` file into a `.uf2` file:

   ```bash
   ../../../bin/uf2conv.py firmware.hex -c -o firmware.uf2 -f 0xADA52840
   ```

4. Copy the new `.uf2` file to the Xiao's mass storage volume:

   ```bash
   cp firmware.uf2 /Volumes/XIAO-BOOT
   ```

5. The Xiao's red LED will flash for several seconds as the firmware is copied.
6. Once the firmware is copied, to verify it is running, run the following command:

   ```bash
   meshtastic --noproto
   ```

7. Then, press the Xiao's `reset` button again. You should see a lot of debug output logged in the terminal window.

&nbsp;

## 6. Troubleshooting

- If after flashing Meshtastic, the Xiao is bootlooped, look at the serial output (you can see this by running `meshtastic --noproto` with the device connected to your computer via USB).

  - If you see that the SX1262 init result was -2, this likely indicates a wiring problem; double check your wiring and pin mapping in `variant.h`.

  - If you see an error mentioning tinyFS, this may mean you need to reformat the Xiao's storage:

    1. Double press the `reset` button to put the Xiao in bootloader mode.

    2. In a terminal window, navigate to the Meshtastic firmware repo's root directory, and from there to `variants/xiao_ble`.

    3. Run the following command: &nbsp;`cp xiao-ble-internal-format.uf2 /Volumes/XIAO-BOOT`

    4. The Xiao's red LED will flash briefly as the filesystem format firmware is copied.

    5. Run the following command: &nbsp;`meshtastic --noproto`

    6. In the output of the above command, you should see a message saying "Formatting...done".

    7. To flash Meshtastic again, repeat the steps in section 5 above.

  - If you don't see any specific error message, but the boot process is stuck or not proceeding as expected, this might also mean there is a conflict in `variant.h`. If you have made any changes to the pin mapping, ensure they do not result in a conflict. If all else fails, try reverting your changes and using the known-good configuration included here.

  - The above might also mean something is wired incorrectly. Try reverting to one of the known-good example wirings in section 4.

- If the E22 gets hot to the touch:
  - The power amplifier is likely running continually. Disconnect it and the Xiao from power immediately, and double check wiring and pin mapping. In my experimentation this occurred in cases where TXEN was inadvertenly high (usually due to a pin mapping conflict).

&nbsp;

## 7. Notes

- There are several anecdotal recommendations regarding the Tx power the E22's internal SX1262 should be set to in order to achieve the advertised output of 30 dBm, ranging from 4 (per <a href="https://github.com/jgromes/RadioLib/wiki/High-power-Radio-Modules-Guide">this article</a> in the RadioLib github repo) to 22 (per <a href="https://discord.com/channels/867578229534359593/871539930852130866/976472577545490482">this conversation</a> from the Meshtastic Discord). When paired with the Xiao BLE in the configurations described above, I observed that the output is at its maximum when Tx power is set to 22.

- To achieve its full output, the E22 should have a bypass capacitor from its 5V supply to ground. 100 µF works well.

- The E22 will happily run on voltages lower than 5V, but the full output power will not be realized. For example, with a fully charged LiPo at 4.2V, Tx power appears to max out around 26-27 dBm.

&nbsp;

## 8. Testing Methodology

During what became a fairly long trial-and-error process, I did a lot of careful testing of Tx power and Rx sensitivity. My methodology in these tests was as follows:

- All tests were conducted between two nodes:

  1. The Xiao BLE + E22 coupled with an <a href="https://www.digikey.com/en/products/detail/abracon-llc/ARRKP4065-S915A/8593263">Abracon ARRKP4065-S915A</a> ceramic patch antenna

  2. A RAK 5005/4631 coupled with a <a href="https://www.streakwave.com/laird-technologies-ma9-5n-55dbi-900mhz-mobile-omni-select-mount">Laird MA9-5N</a> antenna via a 4" U.FL to Type N pigtail.

  - No other nodes were powered up onsite or nearby.

    <br>

- Each node and its antenna was kept in exactly the same position and orientation throughout testing.

- Other environmental factors (e.g. the location and resting position of my body in the room while testing) were controlled as carefully as possible.

- Each test comprised at least five (and often ten) runs, after which the results were averaged.

- All testing was done by sending single-character messages between nodes and observing the received RSSI reported in the message acknowledgement. Messages were sent one by one, waiting for each to be acknowledged or time out before sending the next.

- The E22's Tx power was observed by sending messages from the RAK to the Xiao BLE + E22 and recording the received RSSI.

- The opposite was done to observe the E22's Rx sensitivity: messages were sent from the Xiao BLE + E22 to the RAK, and the received RSSI was recorded.

While this cannot match the level of accuracy achievable with actual test equipment in a lab setting, it was nonetheless sufficient to demonstrate the (sometimes very large) differences in Tx power and Rx sensitivity between various configurations.
