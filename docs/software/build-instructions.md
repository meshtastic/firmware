# Build instructions

This project uses the simple PlatformIO build system. You can use the IDE, but for brevity
in these instructions I describe use of their command line tool.

1. Purchase a suitable radio (see above)
2. Install [PlatformIO](https://platformio.org/platformio-ide)
3. Download this git repo and cd into it:

```
git clone https://github.com/meshtastic/Meshtastic-device.git
cd Meshtastic-device
```

4. Run `git submodule update --init --recursive` to pull in dependencies this project needs.
5. If you are outside the USA, run "export COUNTRY=EU865" (or whatever) to set the correct frequency range for your country. Options are provided for `EU433`, `EU865`, `CN`, `JP` and `US` (default). Pull-requests eagerly accepted for other countries.
6. Plug the radio into your USB port
7. Type `pio run --environment XXX -t upload` (This command will fetch dependencies, build the project and install it on the board via USB). For XXX, use the board type you have (either `tbeam`, `heltec`, `ttgo-lora32-v1`, `ttgo-lora32-v2`).
8. Platform IO also installs a very nice VisualStudio Code based IDE, see their [tutorial](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html) if you'd like to use it.

## Decoding stack traces

If you get a crash, you can decode the addresses from the `Backtrace:` line:

1. Save the `Backtrace: 0x....` line to a file, e.g., `backtrace.txt`.
2. Run `bin/exception_decoder.py backtrace.txt` (this uses symbols from the
   last `firmware.elf`, so you must be running the same binary that's still in
   your `.pio/build` directory).
