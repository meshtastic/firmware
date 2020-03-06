# Build instructions

This project uses the simple PlatformIO build system. You can use the IDE, but for brevity
in these instructions I describe use of their command line tool.

1. Purchase a suitable radio (see above)
2. Install [PlatformIO](https://platformio.org/platformio-ide)
3. Download this git repo and cd into it
4. Edit configuration.h and comment out *one* of the following two lines (depending on which board you are using):
```
// #define T_BEAM_V10  
#define HELTEC_LORA32
```
5. Plug the radio into your USB port
6. Type "pio run -t upload" (This command will fetch dependencies, build the project and install it on the board via USB)
7. Platform IO also installs a very nice VisualStudio Code based IDE, see their [tutorial](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html) if you'd like to use it
