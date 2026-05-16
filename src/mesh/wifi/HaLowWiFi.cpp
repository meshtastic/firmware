#include "configuration.h"
#ifdef USE_HALOW_WIFI

#include "HaLowWiFi.h"

#ifdef USE_MM_IOT_ESP32
extern "C" {
#include "mmwlan.h"
}
#endif

namespace halow
{

HaLowWiFiClass HaLowWiFi;

void HaLowWiFiClass::begin(const char *ssid, const char *passphrase)
{
    (void)ssid;
    (void)passphrase;
#ifdef USE_MM_IOT_ESP32
    // Phase 1: mmwlan_sta_connect(ssid, passphrase, ...).
#endif
}

uint8_t HaLowWiFiClass::status()
{
#ifdef USE_MM_IOT_ESP32
    // Phase 1: translate mmwlan link state to the Arduino-WiFi-compat enum.
    return HALOW_WL_DISCONNECTED;
#else
    return HALOW_WL_DISCONNECTED;
#endif
}

IPAddress HaLowWiFiClass::localIP()
{
    return IPAddress(0, 0, 0, 0);
}

String HaLowWiFiClass::macAddress()
{
    return String("00:00:00:00:00:00");
}

bool HaLowWiFiClass::isConnected()
{
    return status() == HALOW_WL_CONNECTED;
}

} // namespace halow

#endif // USE_HALOW_WIFI
