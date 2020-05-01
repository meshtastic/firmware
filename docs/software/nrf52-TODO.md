# NRF52 TODO

## Initial work items

Minimum items needed to make sure hardware is good.

- add a hard fault handler
- use "variants" to get all gpio bindings
- plug in correct variants for the real board
- Use the PMU driver on real hardware
- add a NEMA based GPS driver to test GPS
- Use new radio driver on real hardware - possibly start with https://os.mbed.com/teams/Semtech/code/SX126xLib/
- Use UC1701 LCD driver on real hardware. Still need to create at startup and probe on SPI
- test the LEDs
- test the buttons
- make a new boarddef with a variant.h file. Fix pins in that file. In particular (at least):
  #define PIN_SPI_MISO (46)
  #define PIN_SPI_MOSI (45)
  #define PIN_SPI_SCK (47)
  #define PIN_WIRE_SDA (26)
  #define PIN_WIRE_SCL (27)

## Secondary work items

Needed to be fully functional at least at the same level of the ESP32 boards. At this point users would probably want them.

- increase preamble length? - will break other clients? so all devices must update
- enable BLE DFU somehow
- set appversion/hwversion
- report appversion/hwversion in BLE
- use new LCD driver from screen.cpp. Still need to hook it to a subclass of (poorly named) OLEDDisplay, and override display() to stream bytes out to the screen.
- get full BLE api working
- we need to enable the external xtal for the sx1262 (on dio3)
- figure out which regulator mode the sx1262 is operating in
- turn on security for BLE, make pairing work
- make power management/sleep work properly
- make a settimeofday implementation
- make a file system implementation (preferably one that can see the files the bootloader also sees) - use https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.sdk5.v15.3.0/lib_fds_usage.html?cp=7_5_0_3_55_3
- make ble endpoints not require "start config", just have them start in config mode
- measure power management and confirm battery life
- use new PMU to provide battery voltage/% full to app (both bluetooth and screen)
- do initial power measurements

## Items to be 'feature complete'

- use SX126x::startReceiveDutyCycleAuto to save power by sleeping and briefly waking to check for preamble bits. Change xmit rules to have more preamble bits.
- turn back on in-radio destaddr checking for RF95
- remove the MeshRadio wrapper - we don't need it anymore, just do everythin in RadioInterface subclasses.
- figure out what the correct current limit should be for the sx1262, currently we just use the default 100
- put sx1262 in sleepmode when processor gets shutdown (or rebooted), ideally even for critical faults (to keep power draw low). repurpose deepsleep state for this.
- good power management tips: https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-power-on-nrf52-designs
- call PMU set_ADC_CONV(0) during sleep, to stop reading PMU adcs and decrease current draw
- do final power measurements
- backport the common PMU API between AXP192 and PmuBQ25703A
- use the new buttons in the UX
- currently using soft device SD140, is that ideal?
- turn on the watchdog timer, require servicing from key application threads
- install a hardfault handler for null ptrs (if one isn't already installed)
- nrf52setup should call randomSeed(tbd)

## Things to do 'someday'

Nice ideas worth considering someday...

- make/find a multithread safe debug logging class (include remote logging and timestamps and levels). make each log event atomic.
- turn on freertos stack size checking
- Currently we use Nordic's vendor ID, which is apparently okay: https://devzone.nordicsemi.com/f/nordic-q-a/44014/using-nordic-vid-and-pid-for-nrf52840 and I just picked a PID of 0x4403
- Use NRF logger module (includes flash logging etc...) instead of DEBUG_MSG
- Use "LED softblink" library on NRF52 to do nice pretty "breathing" LEDs. Don't whack LED from main thread anymore.
- decrease BLE xmit power "At 0dBm with the DC/DC on, the nRF52832 transmitter draws 5.3mA. Increasing the TX power to +4dBm adds only 2.2mA. Decreasing it to -40 dBm saves only 2.6mA."
- in addition to the main CPU watchdog, use the PMU watchdog as a really big emergency hammer
- turn on 'shipping mode' in the PMU when device is 'off' - to cut battery draw to essentially zero
- make Lorro_BQ25703A read/write operations atomic, current version could let other threads sneak in (once we start using threads)
- turn on DFU assistance in the appload using the nordic DFU helper lib call
- make the segger logbuffer larger, move it to RAM that is preserved across reboots and support reading it out at runtime (to allow full log messages to be included in crash reports). Share this code with ESP32 (use gcc noinit attribute)
- convert hardfaults/panics/asserts/wd exceptions into fault codes sent to phone
- stop enumerating all i2c devices at boot, it wastes power & time
- consider using "SYSTEMOFF" deep sleep mode, without RAM retension. Only useful for 'truly off - wake only by button press' only saves 1.5uA vs SYSTEMON. (SYSTEMON only costs 1.5uA). Possibly put PMU into shipping mode?

## Old unorganized notes

## Notes on PCA10059 Dongle

- docs: https://infocenter.nordicsemi.com/pdf/nRF52840_Dongle_User_Guide_v1.0.pdf

- Currently using Nordic PCA10059 Dongle hardware
- https://community.platformio.org/t/same-bootloader-same-softdevice-different-board-different-pins/11411/9

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
