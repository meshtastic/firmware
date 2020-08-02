# NRF52 TODO

- Possibly switch from softdevice to Apachy Newt: https://github.com/espressif/esp-nimble
  https://github.com/apache/mynewt-core - use nimble BLE on both ESP32 and NRF52

## RAK815

TODO:

- shrink soft device RAM usage
- get nrf52832 working again (currently OOM)
- i2c gps comms not quite right
- ble: AdafruitBluefruit::begin - adafruit_ble_task was assigned an invalid stack pointer. out of memory?
- measure power draw

### Bootloader

Install our (temporarily hacked up) adafruit bootloader

```
kevinh@kevin-server:~/development/meshtastic/Adafruit_nRF52_Bootloader$ make BOARD=rak815 sd flash
LD rak815_bootloader-0.3.2-111-g9478eb7-dirty.out
   text	   data	    bss	    dec	    hex	filename
  20888	   1124	  15006	  37018	   909a	_build/build-rak815/rak815_bootloader-0.3.2-111-g9478eb7-dirty.out
Create rak815_bootloader-0.3.2-111-g9478eb7-dirty.hex
Create rak815_bootloader-0.3.2-111-g9478eb7-dirty-nosd.hex
Flashing: rak815_bootloader-0.3.2-111-g9478eb7-dirty-nosd.hex
nrfjprog --program _build/build-rak815/rak815_bootloader-0.3.2-111-g9478eb7-dirty-nosd.hex --sectoranduicrerase -f nrf52 --reset
Parsing hex file.
Erasing page at address 0x0.
Erasing page at address 0x74000.
Erasing page at address 0x75000.
Erasing page at address 0x76000.
Erasing page at address 0x77000.
Erasing page at address 0x78000.
Erasing page at address 0x79000.
Erasing UICR flash area.
Applying system reset.
Checking that the area to write is not protected.
Programming device.
Applying system reset.
Run.
```

### Appload

tips on installing https://github.com/platformio/platform-nordicnrf52/issues/8#issuecomment-374017768

to see console output over jlink:

```
12:17
in one tab run "bin/nrf52832-gdbserver.sh" - leave this running the whole time while developing/debugging
12:17
~/development/meshtastic/meshtastic-esp32$ bin/nrf52-console.sh
###RTT Client: ************************************************************
###RTT Client: *               SEGGER Microcontroller GmbH                *
###RTT Client: *   Solutions for real time microcontroller applications   *
###RTT Client: ************************************************************
###RTT Client: *                                                          *
###RTT Client: *       (c) 2012 - 2016  SEGGER Microcontroller GmbH       *
###RTT Client: *                                                          *
###RTT Client: *     www.segger.com     Support: support@segger.com       *
###RTT Client: *                                                          *
###RTT Client: ************************************************************
###RTT Client: *                                                          *
###RTT Client: * SEGGER J-Link RTT Client   Compiled Apr  7 2020 15:01:22 *
###RTT Client: *                                                          *
###RTT Client: ************************************************************
###RTT Client: -----------------------------------------------
###RTT Client: Connecting to J-Link RTT Server via localhost:19021 ..............
###RTT Client: Connected.
SEGGER J-Link V6.70c - Real time terminal output
SEGGER J-Link ARM V9.6, SN=69663845
Process: JLinkGDBServerCLExein another tab run:
12:18
On NRF52 I've been using the jlink fake serial console.  But since the rak815 has the serial port hooked up we can switch back to that once the basics are working.
```

## Misc work items

RAM investigation.
nRF52832-QFAA 64KB ram, 512KB flash vs
nrf52832-QFAB 32KB ram, 512kb flash
nrf52833 128KB RAM
nrf52840 256KB RAM, 1MB flash

Manual hacks needed to build (for now):

kevinh@kevin-server:~/.platformio/packages/framework-arduinoadafruitnrf52/variants\$ ln -s ~/development/meshtastic/meshtastic-esp32/variants/\* .

## Initial work items

Minimum items needed to make sure hardware is good.

- DONE set power UICR per https://devzone.nordicsemi.com/f/nordic-q-a/28562/nrf52840-regulator-configuration
- switch charge controller into / out of performance mode (see 8.3.1 in datasheet)
- write UC1701 wrapper
- Test hardfault handler for null ptrs (if one isn't already installed)
- test my hackedup bootloader on the real hardware
- Use the PMU driver on real hardware
- Use new radio driver on real hardware
- Use UC1701 LCD driver on real hardware. Still need to create at startup and probe on SPI. Make sure SPI is atomic.
- set vbus voltage per https://infocenter.nordicsemi.com/topic/ps_nrf52840/power.html?cp=4_0_0_4_2
- test the LEDs
- test the buttons

## Secondary work items

Needed to be fully functional at least at the same level of the ESP32 boards. At this point users would probably want them.

- DONE get serial API working
- get full BLE api working
- make power management/sleep work properly
- make a settimeofday implementation
- DONE increase preamble length? - will break other clients? so all devices must update
- DONE enable BLE DFU somehow
- report appversion/hwversion in BLE
- use new LCD driver from screen.cpp. Still need to hook it to a subclass of (poorly named) OLEDDisplay, and override display() to stream bytes out to the screen.
- we need to enable the external tcxo for the sx1262 (on dio3)?
- figure out which regulator mode the sx1262 is operating in
- turn on security for BLE, make pairing work
- make ble endpoints not require "start config", just have them start in config mode
- use new PMU to provide battery voltage/% full to app (both bluetooth and screen)
- do initial power measurements, measure effects of more preamble bits, measure power management and confirm battery life
- set UICR.CUSTOMER to indicate board model & version

## Items to be 'feature complete'

- check datasheet about sx1262 temperature compensation
- enable brownout detection and watchdog
- stop polling for GPS characters, instead stay blocked on read in a thread
- figure out what the correct current limit should be for the sx1262, currently we just use the default 100
- put sx1262 in sleepmode when processor gets shutdown (or rebooted), ideally even for critical faults (to keep power draw low). repurpose deepsleep state for this.
- good power management tips: https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-power-on-nrf52-designs
- call PMU set_ADC_CONV(0) during sleep, to stop reading PMU adcs and decrease current draw
- do final power measurements
- backport the common PMU API between AXP192 and PmuBQ25703A
- use the new buttons in the UX
- currently using soft device SD140, is that ideal?
- turn on the watchdog timer, require servicing from key application threads
- nrf52setup should call randomSeed(tbd)
- implement SYSTEMOFF behavior per https://infocenter.nordicsemi.com/topic/ps_nrf52840/power.html?cp=4_0_0_4_2

## Things to do 'someday'

Nice ideas worth considering someday...

- enable monitor mode debugging (need to use real jlink): https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/monitor-mode-debugging-with-j-link-and-gdbeclipse
- Improve efficiency of PeriodicTimer by only checking the next queued timer event, and carefully sorting based on schedule
- make a Mfg Controller and device under test classes as examples of custom app code for third party devs. Make a post about this. Use a custom payload type code. Have device under test send a broadcast with max hopcount of 0 for the 'mfgcontroller' payload type. mfg controller will read SNR and reply. DOT will declare failure/success and switch to the regular app screen.
- Hook Segger RTT to the nordic logging framework. https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/debugging-with-real-time-terminal
- Use nordic logging for DEBUG_MSG
- use the Jumper simulator to run meshes of simulated hardware: https://docs.jumper.io/docs/install.html
- make/find a multithread safe debug logging class (include remote logging and timestamps and levels). make each log event atomic.
- turn on freertos stack size checking
- Currently we use Nordic's vendor ID, which is apparently okay: https://devzone.nordicsemi.com/f/nordic-q-a/44014/using-nordic-vid-and-pid-for-nrf52840 and I just picked a PID of 0x4403
- Use NRF logger module (includes flash logging etc...) instead of DEBUG_MSG
- Use "LED softblink" library on NRF52 to do nice pretty "breathing" LEDs. Don't whack LED from main thread anymore.
- decrease BLE xmit power "At 0dBm with the DC/DC on, the nRF52832 transmitter draws 5.3mA. Increasing the TX power to +4dBm adds only 2.2mA. Decreasing it to -40 dBm saves only 2.6mA."
- in addition to the main CPU watchdog, use the PMU watchdog as a really big emergency hammer
- turn on 'shipping mode' in the PMU when device is 'off' - to cut battery draw to essentially zero
- make Lorro_BQ25703A read/write operations atomic, current version could let other threads sneak in (once we start using threads)
- make the segger logbuffer larger, move it to RAM that is preserved across reboots and support reading it out at runtime (to allow full log messages to be included in crash reports). Share this code with ESP32 (use gcc noinit attribute)
- convert hardfaults/panics/asserts/wd exceptions into fault codes sent to phone
- stop enumerating all i2c devices at boot, it wastes power & time
- consider using "SYSTEMOFF" deep sleep mode, without RAM retension. Only useful for 'truly off - wake only by button press' only saves 1.5uA vs SYSTEMON. (SYSTEMON only costs 1.5uA). Possibly put PMU into shipping mode?
- change the BLE protocol to be more symmetric. Have the phone _also_ host a GATT service which receives writes to
  'fromradio'. This would allow removing the 'fromnum' mailbox/notify scheme of the current approach and decrease the number of packet handoffs when a packet is received.
- Using the preceeding, make a generalized 'nrf52/esp32 ble to internet' bridge service. To let nrf52 apps do MQTT/UDP/HTTP POST/HTTP GET operations to web services.
- lower advertise interval to save power, lower ble transmit power to save power
- the SX126x class does SPI transfers on a byte by byte basis, which is very ineffecient. Much better to do block writes/reads.

## Old unorganized notes

## Notes on PCA10059 Dongle

- docs: https://infocenter.nordicsemi.com/pdf/nRF52840_Dongle_User_Guide_v1.0.pdf

- Currently using Nordic PCA10059 Dongle hardware
- https://community.platformio.org/t/same-bootloader-same-softdevice-different-board-different-pins/11411/9

- To make Segger JLink more reliable, turn off its fake filesystem. "JLinkExe MSDDisable" per https://learn.adafruit.com/circuitpython-on-the-nrf52/nrf52840-bootloader

## Done

- DONE add "DFU trigger library" to application load
- DONE: using this: Possibly use this bootloader? https://github.com/adafruit/Adafruit_nRF52_Bootloader
- DONE select and install a bootloader (adafruit)
- DONE get old radio driver working on NRF52
- DONE basic test of BLE
- DONE get a debug 'serial' console working via the ICE passthrough feature
- DONE switch to RadioLab? test it with current radio. https://github.com/jgromes/RadioLib
- DONE change rx95 to radiolib
- DONE track rxbad, rxgood, txgood
- DONE neg 7 error code from receive
- DONE remove unused sx1262 lib from github
- at boot we are starting our message IDs at 1, rather we should start them at a random number. also, seed random based on timer. this could be the cause of our first message not seen bug.
- add a NEMA based GPS driver to test GPS
- DONE use "variants" to get all gpio bindings
- DONE plug in correct variants for the real board
- turn on DFU assistance in the appload using the nordic DFU helper lib call
- make a new boarddef with a variant.h file. Fix pins in that file. In particular (at least):
  #define PIN_SPI_MISO (46)
  #define PIN_SPI_MOSI (45)
  #define PIN_SPI_SCK (47)
  #define PIN_WIRE_SDA (26)
  #define PIN_WIRE_SCL (27)
- customize the bootloader to use proper button bindings
- remove the MeshRadio wrapper - we don't need it anymore, just do everything in RadioInterface subclasses.
- DONE use SX126x::startReceiveDutyCycleAuto to save power by sleeping and briefly waking to check for preamble bits. Change xmit rules to have more preamble bits.
- scheduleOSCallback doesn't work yet - it is way too fast (causes rapid polling of busyTx, high power draw etc...)
- find out why we reboot while debugging - it was bluetooth/softdevice
- make a file system implementation (preferably one that can see the files the bootloader also sees) - preferably https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/InternalFileSytem/examples/Internal_ReadWrite/Internal_ReadWrite.ino else use https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.sdk5.v15.3.0/lib_fds_usage.html?cp=7_5_0_3_55_3
- change packet numbers to be 32 bits

```

/*
per
https://docs.platformio.org/en/latest/tutorials/nordicnrf52/arduino_debugging_unit_testing.html

ardunino github is here https://github.com/sandeepmistry/arduino-nRF5
devboard hw docs here:
https://infocenter.nordicsemi.com/topic/ug_nrf52840_dk/UG/nrf52840_DK/hw_buttons_leds.html?cp=4_0_4_7_6

https://docs.platformio.org/en/latest/boards/nordicnrf52/nrf52840_dk_adafruit.html

must install adafruit bootloader first!
https://learn.adafruit.com/circuitpython-on-the-nrf52/nrf52840-bootloader
see link above and turn off jlink filesystem if we see unreliable serial comms
over USBCDC

adafruit bootloader install commands (from their readme)
kevinh@kevin-server:~/.platformio/packages/framework-arduinoadafruitnrf52/bootloader$
nrfjprog -e -f nrf52 Erasing user available code and UICR flash areas. Applying
system reset.
kevinh@kevin-server:~/.platformio/packages/framework-arduinoadafruitnrf52/bootloader$
nrfjprog --program pca10056/pca10056_bootloader-0.3.2_s140_6.1.1.hex -f nrf52
Parsing hex file.
Reading flash area to program to guarantee it is erased.
Checking that the area to write is not protected.
Programming device.
kevinh@kevin-server:~/.platformio/packages/framework-arduinoadafruitnrf52/bootloader$
nrfjprog --reset -f nrf52 Applying system reset. Run.

install jlink tools from here:
https://www.segger.com/downloads/jlink#J-LinkSoftwareAndDocumentationPack

install nrf tools from here:
https://www.nordicsemi.com/Software-and-tools/Development-Tools/nRF-Command-Line-Tools/Download#infotabs

examples of turning off the loop call to save power:
https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/advertising-beacon

example of a more complex BLE service:
https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/custom-hrm
*/

// See g_ADigitalPinMap to see how arduino maps to the real gpio#s - and all in
// P0
#define LED1 14
#define LED2 13

/*
good led ble demo:
https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/Bluefruit52Lib/examples/Peripheral/nrf_blinky/nrf_blinky.ino
*/
```
