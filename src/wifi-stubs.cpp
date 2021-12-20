//#include "mesh/wifi/WebServer.h"
#include "configuration.h"

#ifdef NO_ESP32

//#include "mesh/wifi/WiFiAPClient.h"

bool initWifi(bool forceSoftAP) {
    return false;
}

void deinitWifi() {}

bool isWifiAvailable()
{
    return false;
}

#endif
