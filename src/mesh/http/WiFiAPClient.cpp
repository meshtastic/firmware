#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "mesh/http/WiFiAPClient.h"
#include "configuration.h"
#include "main.h"
#include "mesh/http/WebServer.h"
#include "mesh/wifi/WiFiServerAPI.h"
#include "mqtt/MQTT.h"
#include "target_specific.h"
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#ifndef DISABLE_NTP
#include <NTPClient.h>
#endif

using namespace concurrency;

static void WiFiEvent(WiFiEvent_t event);

// NTP
WiFiUDP ntpUDP;

#ifndef DISABLE_NTP
NTPClient timeClient(ntpUDP, config.network.ntp_server);
#endif

uint8_t wifiDisconnectReason = 0;

// Stores our hostname
char ourHost[16];

bool APStartupComplete = 0;

unsigned long lastrun_ntp = 0;

bool needReconnect = true; // If we create our reconnector, run it once at the beginning

Periodic *wifiReconnect;

static int32_t reconnectWiFi()
{
    const char *wifiName = config.network.wifi_ssid;
    const char *wifiPsw = config.network.wifi_psk;

    if (config.network.wifi_enabled && needReconnect) {
        
        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        needReconnect = false;

        // Make sure we clear old connection credentials
        WiFi.disconnect(false, true);

        DEBUG_MSG("Reconnecting to WiFi access point %s\n",wifiName);

        WiFi.mode(WIFI_MODE_STA);
        WiFi.begin(wifiName, wifiPsw);
    }

#ifndef DISABLE_NTP
    if (WiFi.isConnected() && (((millis() - lastrun_ntp) > 43200000) || (lastrun_ntp == 0))) { // every 12 hours
        DEBUG_MSG("Updating NTP time from %s\n",config.network.ntp_server);
        if (timeClient.update()) {
            DEBUG_MSG("NTP Request Success - Setting RTCQualityNTP if needed\n");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);
            lastrun_ntp = millis();

        } else {
            DEBUG_MSG("NTP Update failed\n");
        }
    }
#endif

    if (config.network.wifi_enabled && !WiFi.isConnected()) {
        return 1000; // check once per second
    } else {
        return 300000; // every 5 minutes
    }
}

bool isWifiAvailable()
{

    if (config.network.wifi_enabled && (config.network.wifi_ssid[0])) {
        return true;
    } else {
        return false;
    }
}

// Disable WiFi
void deinitWifi()
{
    DEBUG_MSG("WiFi deinit\n");

    if (isWifiAvailable()) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_MODE_NULL);
        DEBUG_MSG("WiFi Turned Off\n");
        // WiFi.printDiag(Serial);
    }
}

static void onNetworkConnected()
{
    if (!APStartupComplete) {
        // Start web server
        DEBUG_MSG("Starting network services\n");

        // start mdns
        if (!MDNS.begin("Meshtastic")) {
            DEBUG_MSG("Error setting up MDNS responder!\n");
        } else {
            DEBUG_MSG("mDNS responder started\n");
            DEBUG_MSG("mDNS Host: Meshtastic.local\n");
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("https", "tcp", 443);
        }

#ifndef DISABLE_NTP
        DEBUG_MSG("Starting NTP time client\n");
        timeClient.begin();
        timeClient.setUpdateInterval(60 * 60); // Update once an hour
#endif

        initWebServer();
        initApiServer();

        APStartupComplete = true;
    }

    // FIXME this is kinda yucky, instead we should just have an observable for 'wifireconnected'
    if (mqtt)
        mqtt->reconnect();
}

// Startup WiFi
bool initWifi()
{
    if (config.network.wifi_enabled && config.network.wifi_ssid[0]) {

        const char *wifiName = config.network.wifi_ssid;
        const char *wifiPsw = config.network.wifi_psk;

        createSSLCert();

        esp_wifi_set_storage(WIFI_STORAGE_RAM); // Disable flash storage for WiFi credentials

        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        if (*wifiName) {
            uint8_t dmac[6];
            getMacAddr(dmac);
            sprintf(ourHost, "Meshtastic-%02x%02x", dmac[4], dmac[5]);

            WiFi.mode(WIFI_MODE_STA);
            WiFi.setHostname(ourHost);
            WiFi.onEvent(WiFiEvent);
            WiFi.setAutoReconnect(false);
            WiFi.setSleep(false);
            if (config.network.eth_mode == Config_NetworkConfig_EthMode_STATIC && config.network.ipv4_config.ip != 0) {
                WiFi.config(config.network.ipv4_config.ip,
                            config.network.ipv4_config.gateway,
                            config.network.ipv4_config.subnet,
                            config.network.ipv4_config.dns, 
                            config.network.ipv4_config.dns); // Wifi wants two DNS servers... set both to the same value
            }

            // This is needed to improve performance.
            esp_wifi_set_ps(WIFI_PS_NONE); // Disable radio power saving

            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    Serial.print("WiFi lost connection. Reason: ");
                    Serial.println(info.wifi_sta_disconnected.reason);

                    /*
                        If we are disconnected from the AP for some reason,
                        save the error code.

                        For a reference to the codes:
                            https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
                    */
                    wifiDisconnectReason = info.wifi_sta_disconnected.reason;
                },
                WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

            DEBUG_MSG("JOINING WIFI soon: ssid=%s\n", wifiName);
            wifiReconnect = new Periodic("WifiConnect", reconnectWiFi);
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
    DEBUG_MSG("WiFi-Event %d: ", event);

    switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
        DEBUG_MSG("WiFi interface ready\n");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        DEBUG_MSG("Completed scan for access points\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        DEBUG_MSG("WiFi station started\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        DEBUG_MSG("WiFi station stopped\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        DEBUG_MSG("Connected to access point\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        DEBUG_MSG("Disconnected from WiFi access point\n");
        WiFi.disconnect(false, true);
        needReconnect = true;
        wifiReconnect->setIntervalFromNow(1000);
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        DEBUG_MSG("Authentication mode of access point has changed\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        DEBUG_MSG("Obtained IP address: ");
        Serial.println(WiFi.localIP());
        onNetworkConnected();
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        DEBUG_MSG("Obtained IP6 address: ");
        Serial.println(WiFi.localIPv6());
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        DEBUG_MSG("Lost IP address and IP address is reset to 0\n");
        WiFi.disconnect(false, true);
        needReconnect = true;
        wifiReconnect->setIntervalFromNow(1000);
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        DEBUG_MSG("WiFi Protected Setup (WPS): succeeded in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        DEBUG_MSG("WiFi Protected Setup (WPS): failed in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        DEBUG_MSG("WiFi Protected Setup (WPS): timeout in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        DEBUG_MSG("WiFi Protected Setup (WPS): pin code in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_PBC_OVERLAP:
        DEBUG_MSG("WiFi Protected Setup (WPS): push button overlap in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        DEBUG_MSG("WiFi access point started\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        DEBUG_MSG("WiFi access point stopped\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        DEBUG_MSG("Client connected\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        DEBUG_MSG("Client disconnected\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        DEBUG_MSG("Assigned IP address to client\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        DEBUG_MSG("Received probe request\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        DEBUG_MSG("IPv6 is preferred\n");
        break;
    case ARDUINO_EVENT_WIFI_FTM_REPORT:
        DEBUG_MSG("Fast Transition Management report\n");
        break;
    case ARDUINO_EVENT_ETH_START:
        DEBUG_MSG("Ethernet started\n");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        DEBUG_MSG("Ethernet stopped\n");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        DEBUG_MSG("Ethernet connected\n");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        DEBUG_MSG("Ethernet disconnected\n");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        DEBUG_MSG("Obtained IP address (ARDUINO_EVENT_ETH_GOT_IP)\n");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        DEBUG_MSG("Obtained IP6 address (ARDUINO_EVENT_ETH_GOT_IP6)\n");
        break;
    case ARDUINO_EVENT_SC_SCAN_DONE:
        DEBUG_MSG("SmartConfig: Scan done\n");
        break;
    case ARDUINO_EVENT_SC_FOUND_CHANNEL:
        DEBUG_MSG("SmartConfig: Found channel\n");
        break;
    case ARDUINO_EVENT_SC_GOT_SSID_PSWD:
        DEBUG_MSG("SmartConfig: Got SSID and password\n");
        break;
    case ARDUINO_EVENT_SC_SEND_ACK_DONE:
        DEBUG_MSG("SmartConfig: Send ACK done\n");
        break;
    case ARDUINO_EVENT_PROV_INIT:
        DEBUG_MSG("Provisioning: Init\n");
        break;
    case ARDUINO_EVENT_PROV_DEINIT:
        DEBUG_MSG("Provisioning: Stopped\n");
        break;
    case ARDUINO_EVENT_PROV_START:
        DEBUG_MSG("Provisioning: Started\n");
        break;
    case ARDUINO_EVENT_PROV_END:
        DEBUG_MSG("Provisioning: End\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
        DEBUG_MSG("Provisioning: Credentials received\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
        DEBUG_MSG("Provisioning: Credentials failed\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        DEBUG_MSG("Provisioning: Credentials success\n");
        break;
    default:
        break;
    }
}

uint8_t getWifiDisconnectReason()
{
    return wifiDisconnectReason;
}
