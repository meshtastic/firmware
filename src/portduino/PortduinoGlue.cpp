#include "CryptoEngine.h"
#include "target_specific.h"
#include <Utility.h>

// FIXME - move getMacAddr/setBluetoothEnable into a HALPlatform class

uint32_t hwId; // fixme move into portduino

void getMacAddr(uint8_t *dmac)
{
    if (!hwId) {
        notImplemented("getMacAddr");
        hwId = random();
    }

    dmac[0] = 0x80;
    dmac[1] = 0;
    dmac[2] = 0;
    dmac[3] = hwId >> 16;
    dmac[4] = hwId >> 8;
    dmac[5] = hwId & 0xff;
}

void setBluetoothEnable(bool on)
{
    notImplemented("setBluetoothEnable");
}

// FIXME - implement real crypto for linux
CryptoEngine *crypto = new CryptoEngine();

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");