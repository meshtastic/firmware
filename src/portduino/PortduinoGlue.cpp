#include "CryptoEngine.h"
#include "target_specific.h"
#include <Utility.h>

// FIXME - move getMacAddr/setBluetoothEnable into a HALPlatform class

void getMacAddr(uint8_t *dmac)
{
    notImplemented("getMacAddr");
}

void setBluetoothEnable(bool on)
{
    notImplemented("setBluetoothEnable");
}

// FIXME - implement real crypto for linux
CryptoEngine *crypto = new CryptoEngine();

void updateBatteryLevel(uint8_t level) NOT_IMPLEMENTED("updateBatteryLevel");