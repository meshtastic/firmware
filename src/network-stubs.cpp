#include "configuration.h"

#if (HAS_WIFI == 0)

bool initWifi()
{
    return false;
}

void deinitWifi() {}

void suspendWifi() {}

void resumeWifi() {}

bool isWifiSuspended()
{
    return false;
}

bool isWifiAvailable()
{
    return false;
}

#endif

#if (HAS_ETHERNET == 0)

bool initEthernet()
{
    return false;
}

bool isEthernetAvailable()
{
    return false;
}

#endif
