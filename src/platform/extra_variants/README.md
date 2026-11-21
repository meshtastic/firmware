# About extra_variants

This directory tree is designed to solve two problems.

- The ESP32 arduino/platformio project doesn't support the nice "if initVariant() is found, call that after init" behavior of the nrf52 builds (they use initVariant() internally).
- Over the years a lot of 'board specific' init code has been added to init() in main.cpp. It would be great to have a general/clean mechanism to allow developers to specify board specific/unique code in a clean fashion without mucking in main.

So we are borrowing the initVariant() ideas here (by using weak gcc references). You can now define earlyInitVariant() and lateInitVariant() if your board needs them. earlyInitVariant() runs at the beginning of setup() directly after waitUntilPowerLevelSafe(); while lateInitVariant() runs after the LoRa radio is initialized.

**Important:** earlyInitVariant() runs _before_ consoleInit(), so the logging subsystem isn't set up yet. Calling a `LOG_*` macro there **crashes the device** - it is not a silent no-op. Never use `LOG_*` in earlyInitVariant(); defer any logging to lateInitVariant() or later.

Board specific code belongs in the board's own variant directory (`variants/<arch>/<board>/variant.cpp`), with that directory added to the env's `build_src_filter` (`+<../variants/<arch>/<board>>`).
Code shared by more than one variant directory can live here instead. Guard it with a define from the boards' variant.h so it is only built for those boards.

This approach has no added runtime cost.
