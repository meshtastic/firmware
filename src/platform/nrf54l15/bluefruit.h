// bluefruit.h — stub for nRF54L15/Zephyr
// NodeDB.cpp includes this when ARCH_NRF52 is defined.
// Bluetooth is excluded (MESHTASTIC_EXCLUDE_BLUETOOTH=1); this satisfies
// the include chain without pulling in the Adafruit Bluefruit SDK.
#pragma once

struct BLEPeripheral {
    void clearBonds() {}
};
struct BLECentral {
    void clearBonds() {}
};

struct BlueFruitClass {
    BLEPeripheral Periph;
    BLECentral    Central;
};

extern BlueFruitClass Bluefruit;
