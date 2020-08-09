# Build instructions

This project uses the simple PlatformIO build system. PlatformIO is an extension to Microsoft VSCode.

## GUI
1. Purchase a suitable [radio](https://github.com/meshtastic/Meshtastic-device/wiki/Hardware-Information).
2. Install [PlatformIO](https://platformio.org/platformio-ide).
3. Click the PlatformIO icon on the side bar. ![platformio icon](https://user-images.githubusercontent.com/47490997/89482668-77c7ea00-d7ee-11ea-8785-5faf8ff99800.png)
4. Under `Quick Access, Miscellaneous, Clone Git Project` enter the URL of the Meshtastic repo found [here](https://github.com/meshtastic/Meshtastic-device). ![image](https://user-images.githubusercontent.com/47490997/89483047-4c91ca80-d7ef-11ea-91f4-1d53d4e8acd9.png) 
5. Select a file location to save the repo.
6. Once loaded, open the `platformio.ini` file. 
7. At the line `default_envs` you can change it to the board type you are building for ie. `tlora-v2, tlora-v1, tlora-v2-1-1.6, tbeam, heltec, tbeam0.7` (boards are listed further down in the file).
8. Click the PlatformIO icon on the side bar. Under `Project Tasks` you can now build or upload.

## Command Line
1. Purchase a suitable [radio](https://github.com/meshtastic/Meshtastic-device/wiki/Hardware-Information).
2. Install [PlatformIO](https://platformio.org/platformio-ide)
3. Download this git repo and cd into it. `git clone https://github.com/meshtastic/Meshtastic-device.git` and `cd Meshtastic-device`
4. Run `git submodule update --init --recursive` to pull in dependencies this project needs.
5. If you are outside the USA, edit [platformio.ini](/platformio.ini) to set the correct frequency range for your country. The line you need to change starts with `hw_version` and instructions are provided above that line. Options are provided for `EU433`, `EU835`, `CN`, `JP` and `US` (default). Pull-requests eagerly accepted for other countries.
6. Plug the radio into your USB port
7. Type `pio run --environment XXX -t upload` (This command will fetch dependencies, build the project and install it on the board via USB). For XXX, use the board type you have (either `tlora-v2, tlora-v1, tlora-v2-1-1.6, tbeam, heltec, tbeam0.7`).
8. Platform IO also installs a very nice VisualStudio Code based IDE, see their [tutorial](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html) if you'd like to use it.

## Decoding stack traces

If you get a crash, you can decode the addresses from the `Backtrace:` line:

1. Save the `Backtrace: 0x....` line to a file, e.g., `backtrace.txt`.
2. Run `bin/exception_decoder.py backtrace.txt` (this uses symbols from the
   last `firmware.elf`, so you must be running the same binary that's still in
   your `.pio/build` directory).
