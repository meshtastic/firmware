#pragma once
#ifdef USE_HALOW_WIFI

#include <IPAddress.h>
#include <WString.h>
#include <stdint.h>

// Subset of the Arduino WiFi API that WiFiAPClient.cpp and UdpMulticastHandler
// touch, backed by Morse Micro mmwlan STA-mode association. Lets the existing
// mesh-over-IP path ride HaLow with no changes to call sites — required while
// the peer-broadcast HaLowInterface remains gated on SDK work.

namespace halow
{

enum WiFiStatusCompat : uint8_t {
    HALOW_WL_IDLE_STATUS = 0,
    HALOW_WL_NO_SSID_AVAIL = 1,
    HALOW_WL_CONNECTED = 3,
    HALOW_WL_DISCONNECTED = 6,
};

class HaLowWiFiClass
{
  public:
    void begin(const char *ssid, const char *passphrase);
    uint8_t status();
    IPAddress localIP();
    String macAddress();
    bool isConnected();
};

extern HaLowWiFiClass HaLowWiFi;

} // namespace halow

// When the HaLow variant is being built, redirect the Arduino WiFi handle
// expected by call sites to the HaLow shim. Including this header in the
// preprocessor-conditional places that currently #include <WiFi.h> is enough
// to swap the implementation at build time.
#define WiFi ::halow::HaLowWiFi
#define WL_CONNECTED ::halow::HALOW_WL_CONNECTED
#define WL_NO_SSID_AVAIL ::halow::HALOW_WL_NO_SSID_AVAIL
#define WL_DISCONNECTED ::halow::HALOW_WL_DISCONNECTED
#define WL_IDLE_STATUS ::halow::HALOW_WL_IDLE_STATUS

#endif // USE_HALOW_WIFI
