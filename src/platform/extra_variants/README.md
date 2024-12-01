# About extra_variants

This directory tree is designed to solve two problems.

- The ESP32 arduino/platformio project doesn't support the nice "if initVariant() is found, call that after init" behavior of the nrf52 builds (they use initVariant() internally).
- Over the years a lot of 'board specific' init code has been added to init() in main.cpp. It would be great to have a general/clean mechanism to allow developers to specify board specific/unique code in a clean fashion without mucking in main.

So we are borrowing the initVariant() ideas here (by using weak gcc references). You can now define lateInitVariant() if your board needs it.

If you'd like a board specific variant to be run, add the variant.cpp file to an appropriately named
subdirectory and check for \_VARIANT_boardname in the cpp file (so that your code is only built for your board).
You'll need to define \_VARIANT_boardname in your corresponding variant.h file.
See existing boards for examples.

This approach has no added runtime cost.
