#include "configuration.h"

#ifndef ARCH_ESP32

bool initWifi() {
    return false;
}

void deinitWifi() {}

bool isWifiAvailable()
{
    return false;
}

#endif
