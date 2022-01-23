#include "mesh/http/WiFiAPClient.h"
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "main.h"
#include "mesh/http/WebServer.h"
#include "mesh/wifi/WiFiServerAPI.h"
#include "mqtt/MQTT.h"
#include "target_specific.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>

using namespace concurrency;

static void WiFiEvent(WiFiEvent_t event);

// DNS Server for the Captive Portal
DNSServer dnsServer;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "0.pool.ntp.org");

uint8_t wifiDisconnectReason = 0;

// Stores our hostname
char ourHost[16];

bool forcedSoftAP = 0;

bool APStartupComplete = 0;

static bool needReconnect = true; // If we create our reconnector, run it once at the beginning

// FIXME, veto light sleep if we have a TCP server running
#if 0
class WifiSleepObserver : public Observer<uint32_t> {
protected:

    /// Return 0 if sleep is okay
    virtual int onNotify(uint32_t newValue) {

    }
};

static WifiSleepObserver wifiSleepObserver;
//preflightSleepObserver.observe(&preflightSleep);
#endif

static int32_t reconnectWiFi()
{
    const char *wifiName = radioConfig.preferences.wifi_ssid;
    const char *wifiPsw = radioConfig.preferences.wifi_password;

    if (radioConfig.has_preferences && needReconnect && !WiFi.isConnected()) {

        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        if (*wifiName) {
            needReconnect = false;

            DEBUG_MSG("... Reconnecting to WiFi access point\n");
            WiFi.mode(WIFI_MODE_STA);
            WiFi.begin(wifiName, wifiPsw);
            

            // Starting timeClient;
        }
    }

    //if (*wifiName) {
    if (WiFi.isConnected()) {
        DEBUG_MSG("Updating NTP time\n");
        if (timeClient.update()) {
            DEBUG_MSG("NTP Request Success - Setting RTCQualityNTP if needed\n");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);

        } else {
            DEBUG_MSG("NTP Update failed\n");
        }
    }

    return 30 * 1000; // every 30 seconds
}

static Periodic *wifiReconnect;

bool isSoftAPForced()
{
    return forcedSoftAP;
}

bool isWifiAvailable()
{
    // If wifi status is connected, return true regardless of the radio configuration.
    if (isSoftAPForced()) {
        return true;
    }

    const char *wifiName = radioConfig.preferences.wifi_ssid;

    if (*wifiName) {
        return true;
    } else {
        return false;
    }
}

// Disable WiFi
void deinitWifi()
{
    /*
        Note from Jm (jm@casler.org - Sept 16, 2020):

        A bug in the ESP32 SDK was introduced in Oct 2019 that keeps the WiFi radio from
        turning back on after it's shut off. See:
            https://github.com/espressif/arduino-esp32/issues/3522

        Until then, WiFi should only be allowed when there's no power
        saving on the 2.4g transceiver.
    */

    DEBUG_MSG("WiFi deinit\n");

    if (isWifiAvailable()) {
        WiFi.mode(WIFI_MODE_NULL);
        DEBUG_MSG("WiFi Turned Off\n");
        // WiFi.printDiag(Serial);
    }
}

static void onNetworkConnected()
{
    if (!APStartupComplete) {
        // Start web server
        DEBUG_MSG("... Starting network services\n");

        // start mdns
        if (!MDNS.begin("Meshtastic")) {
            DEBUG_MSG("Error setting up MDNS responder!\n");
        } else {
            DEBUG_MSG("mDNS responder started\n");
            DEBUG_MSG("mDNS Host: Meshtastic.local\n");
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("https", "tcp", 443);
        }

        DEBUG_MSG("Starting NTP time client\n");
        timeClient.begin();
        timeClient.setUpdateInterval(60*60); // Update once an hour

        initWebServer();
        initApiServer();

        APStartupComplete = true;
    }

    // FIXME this is kinda yucky, instead we should just have an observable for 'wifireconnected'
    if (mqtt)
        mqtt->reconnect();
}

// Startup WiFi
bool initWifi(bool forceSoftAP)
{
    forcedSoftAP = forceSoftAP;

    if ((radioConfig.has_preferences && radioConfig.preferences.wifi_ssid[0]) || forceSoftAP) {
        const char *wifiName = radioConfig.preferences.wifi_ssid;
        const char *wifiPsw = radioConfig.preferences.wifi_password;

        if (forceSoftAP) {
            DEBUG_MSG("WiFi ... Forced AP Mode\n");
        } else if (radioConfig.preferences.wifi_ap_mode) {
            DEBUG_MSG("WiFi ... AP Mode\n");
        } else {
            DEBUG_MSG("WiFi ... Client Mode\n");
        }

        createSSLCert();

        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        if (*wifiName || forceSoftAP) {
            if (radioConfig.preferences.wifi_ap_mode || forceSoftAP) {

                IPAddress apIP(192, 168, 42, 1);
                WiFi.onEvent(WiFiEvent);
                WiFi.mode(WIFI_AP);

                if (forcedSoftAP) {
                    const char *softAPssid = "meshtasticAdmin";
                    const char *softAPpasswd = "12345678";

                    DEBUG_MSG("Starting (Forced) WIFI AP: ssid=%s, ok=%d\n", softAPssid, WiFi.softAP(softAPssid, softAPpasswd));

                } else {
                    DEBUG_MSG("Starting WIFI AP: ssid=%s, ok=%d\n", wifiName, WiFi.softAP(wifiName, wifiPsw));
                }

                WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
                DEBUG_MSG("MY IP AP ADDRESS: %s\n", WiFi.softAPIP().toString().c_str());

                // This is needed to improve performance.
                esp_wifi_set_ps(WIFI_PS_NONE); // Disable radio power saving

                dnsServer.start(53, "*", apIP);

            } else {
                uint8_t dmac[6];
                getMacAddr(dmac);
                sprintf(ourHost, "Meshtastic-%02x%02x", dmac[4], dmac[5]);

                WiFi.mode(WIFI_MODE_STA);
                WiFi.setHostname(ourHost);
                WiFi.onEvent(WiFiEvent);

                // This is needed to improve performance.
                esp_wifi_set_ps(WIFI_PS_NONE); // Disable radio power saving

                WiFi.onEvent(
                    [](WiFiEvent_t event, WiFiEventInfo_t info) {
                        Serial.print("\nWiFi lost connection. Reason: ");
                        Serial.println(info.disconnected.reason);

                        /*
                           If we are disconnected from the AP for some reason,
                           save the error code.

                           For a reference to the codes:
                             https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
                        */
                        wifiDisconnectReason = info.disconnected.reason;
                    },
                    WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

                DEBUG_MSG("JOINING WIFI soon: ssid=%s\n", wifiName);
                wifiReconnect = new Periodic("WifiConnect", reconnectWiFi);
            }
        }
        return true;
    } else {
        DEBUG_MSG("Not using WIFI\n");
        return false;
    }
}

// Called by the Espressif SDK to
static void WiFiEvent(WiFiEvent_t event)
{
    DEBUG_MSG("************ [WiFi-event] event: %d ************\n", event);

    switch (event) {
    case SYSTEM_EVENT_WIFI_READY:
        DEBUG_MSG("WiFi interface ready\n");
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        DEBUG_MSG("Completed scan for access points\n");
        break;
    case SYSTEM_EVENT_STA_START:
        DEBUG_MSG("WiFi station started\n");
        break;
    case SYSTEM_EVENT_STA_STOP:
        DEBUG_MSG("WiFi station stopped\n");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        DEBUG_MSG("Connected to access point\n");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        DEBUG_MSG("Disconnected from WiFi access point\n");
        // Event 5

        needReconnect = true;
        break;
    case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
        DEBUG_MSG("Authentication mode of access point has changed\n");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        DEBUG_MSG("Obtained IP address: ");
        Serial.println(WiFi.localIP());
        onNetworkConnected();
        break;
    case SYSTEM_EVENT_STA_LOST_IP:
        DEBUG_MSG("Lost IP address and IP address is reset to 0\n");
        break;
    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
        DEBUG_MSG("WiFi Protected Setup (WPS): succeeded in enrollee mode\n");
        break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
        DEBUG_MSG("WiFi Protected Setup (WPS): failed in enrollee mode\n");
        break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
        DEBUG_MSG("WiFi Protected Setup (WPS): timeout in enrollee mode\n");
        break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
        DEBUG_MSG("WiFi Protected Setup (WPS): pin code in enrollee mode\n");
        break;
    case SYSTEM_EVENT_AP_START:
        DEBUG_MSG("WiFi access point started\n");

        onNetworkConnected();
        break;
    case SYSTEM_EVENT_AP_STOP:
        DEBUG_MSG("WiFi access point stopped\n");
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        DEBUG_MSG("Client connected\n");
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        DEBUG_MSG("Client disconnected\n");
        break;
    case SYSTEM_EVENT_AP_STAIPASSIGNED:
        DEBUG_MSG("Assigned IP address to client\n");
        break;
    case SYSTEM_EVENT_AP_PROBEREQRECVED:
        DEBUG_MSG("Received probe request\n");
        break;
    case SYSTEM_EVENT_GOT_IP6:
        DEBUG_MSG("IPv6 is preferred\n");
        break;
    case SYSTEM_EVENT_ETH_START:
        DEBUG_MSG("Ethernet started\n");
        break;
    case SYSTEM_EVENT_ETH_STOP:
        DEBUG_MSG("Ethernet stopped\n");
        break;
    case SYSTEM_EVENT_ETH_CONNECTED:
        DEBUG_MSG("Ethernet connected\n");
        break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
        DEBUG_MSG("Ethernet disconnected\n");
        break;
    case SYSTEM_EVENT_ETH_GOT_IP:
        DEBUG_MSG("Obtained IP address (SYSTEM_EVENT_ETH_GOT_IP)\n");
        break;
    default:
        break;
    }
}

void handleDNSResponse()
{
    if (radioConfig.preferences.wifi_ap_mode || isSoftAPForced()) {
        dnsServer.processNextRequest();
    }
}

uint8_t getWifiDisconnectReason()
{
    return wifiDisconnectReason;
}