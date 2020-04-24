# Initial work items

Minimum items needed to make sure hardware is good.

- DONE select and install a bootloader (adafruit)
- DONE get old radio driver working on NRF52
- DONE basic test of BLE
- DONE get a debug 'serial' console working via the ICE passthrough feater
- add PMU driver
- add new radio driver - possibly start with https://os.mbed.com/teams/Semtech/code/SX126xLib/
- add LCD driver
- test the LEDs
- test the buttons
- make a new boarddef with a variant.h file. Fix pins in that file. In particular (at least):
  #define PIN_SPI_MISO (46)
  #define PIN_SPI_MOSI (45)
  #define PIN_SPI_SCK (47)
  #define PIN_WIRE_SDA (26)
  #define PIN_WIRE_SCL (27)

# Secondary work items

Needed to be fully functional at least at the same level of the ESP32 boards. At this point users would probably want them.

- get full BLE api working
- we need to enable the external xtal for the sx1262 (on dio3)
- figure out which regulator mode the sx1262 is operating in
- turn on security for BLE, make pairing work
- make power management/sleep work properly
- make a settimeofday implementation
- make a file system implementation (preferably one that can see the files the bootloader also sees)
- make ble endpoints not require "start config", jsut have them start in config mode
- measure power management and confirm battery life

# Items to be 'feature complete'

- use the new buttons in the UX
- currently using soft device SD140, is that ideal?

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
