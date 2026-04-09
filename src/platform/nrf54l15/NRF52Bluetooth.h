// NRF52Bluetooth.h — stub for nRF54L15/Zephyr
// main.h includes this when ARCH_NRF52 is defined.
// Bluetooth is excluded (MESHTASTIC_EXCLUDE_BLUETOOTH=1); this satisfies the
// include chain without pulling in the nRF52 Bluefruit SDK.
#pragma once

class NRF52Bluetooth {
  public:
    void setup()            {}
    void shutdown()         {}
    void startDisabled()    {}
    void resumeAdvertising(){}
    void clearBonds()       {}
    bool isConnected()      { return false; }
    int  getRssi()          { return 0; }
    void sendLog(const uint8_t *, size_t) {}
};
