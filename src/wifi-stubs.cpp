//#include "mesh/wifi/WebServer.h"
#include "configuration.h"

#ifdef NO_ESP32

//#include "mesh/wifi/WiFiAPClient.h"

void initWifi(bool forceSoftAP) {}

void deinitWifi() {}

bool isWifiAvailable()
{
    return false;
}

#endif
