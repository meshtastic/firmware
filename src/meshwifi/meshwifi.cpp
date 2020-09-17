#include "meshwifi.h"
#include <WiFi.h>
#include "configuration.h"
#include "main.h"
#include "NodeDB.h"
#include "meshwifi/meshhttp.h"

bool isWifiAvailable() 
{
    const char *wifiName = radioConfig.preferences.wifi_ssid;
    const char *wifiPsw = radioConfig.preferences.wifi_password;

    if (*wifiName && *wifiPsw) {
        return 1;
    } else {
        return 0;
    }
}

// Disable WiFi
void deinitWifi()
{
    WiFi.mode(WIFI_MODE_NULL);
    DEBUG_MSG("WiFi Turned Off\n");
}


// Startup WiFi
void initWifi()
{

    if (isWifiAvailable() == 0) {
        return;
    }

    //strcpy(radioConfig.preferences.wifi_ssid, WiFi_SSID_NAME);
    //strcpy(radioConfig.preferences.wifi_password, WiFi_SSID_PASSWORD);
    if (radioConfig.has_preferences) {
        const char *wifiName = radioConfig.preferences.wifi_ssid;

        if (*wifiName) {
            const char *wifiPsw = radioConfig.preferences.wifi_password;
            if (radioConfig.preferences.wifi_ap_mode) {
                DEBUG_MSG("STARTING WIFI AP: ssid=%s, ok=%d\n", wifiName, WiFi.softAP(wifiName, wifiPsw));
            } else {
                WiFi.mode(WIFI_MODE_STA);
                WiFi.onEvent(WiFiEvent);

                DEBUG_MSG("JOINING WIFI: ssid=%s\n", wifiName);
                if (WiFi.begin(wifiName, wifiPsw) == WL_CONNECTED) {
                    DEBUG_MSG("MY IP ADDRESS: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    DEBUG_MSG("Started Joining WIFI\n");
                }
            }
        }
    } else
        DEBUG_MSG("Not using WIFI\n");
}


void WiFiEvent(WiFiEvent_t event)
{
    DEBUG_MSG("************ [WiFi-event] event: %d ************\n", event);

    switch (event) {
        case SYSTEM_EVENT_WIFI_READY: 
            DEBUG_MSG("WiFi interface ready");
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            DEBUG_MSG("Completed scan for access points");
            break;
        case SYSTEM_EVENT_STA_START:
            DEBUG_MSG("WiFi client started");
            break;
        case SYSTEM_EVENT_STA_STOP:
            DEBUG_MSG("WiFi clients stopped");
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            DEBUG_MSG("Connected to access point");
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            DEBUG_MSG("Disconnected from WiFi access point");

            // Reconnect WiFi
            reconnectWiFi();
            break;
        case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
            DEBUG_MSG("Authentication mode of access point has changed");
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            DEBUG_MSG("Obtained IP address: ");
            Serial.println(WiFi.localIP());

            // Start web server
            initWebServer();
            
            break;
        case SYSTEM_EVENT_STA_LOST_IP:
            DEBUG_MSG("Lost IP address and IP address is reset to 0");
            break;
        case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
            DEBUG_MSG("WiFi Protected Setup (WPS): succeeded in enrollee mode");
            break;
        case SYSTEM_EVENT_STA_WPS_ER_FAILED:
            DEBUG_MSG("WiFi Protected Setup (WPS): failed in enrollee mode");
            break;
        case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
            DEBUG_MSG("WiFi Protected Setup (WPS): timeout in enrollee mode");
            break;
        case SYSTEM_EVENT_STA_WPS_ER_PIN:
            DEBUG_MSG("WiFi Protected Setup (WPS): pin code in enrollee mode");
            break;
        case SYSTEM_EVENT_AP_START:
            DEBUG_MSG("WiFi access point started");
            break;
        case SYSTEM_EVENT_AP_STOP:
            DEBUG_MSG("WiFi access point stopped");
            break;
        case SYSTEM_EVENT_AP_STACONNECTED:
            DEBUG_MSG("Client connected");
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            DEBUG_MSG("Client disconnected");
            break;
        case SYSTEM_EVENT_AP_STAIPASSIGNED:
            DEBUG_MSG("Assigned IP address to client");
            break;
        case SYSTEM_EVENT_AP_PROBEREQRECVED:
            DEBUG_MSG("Received probe request");
            break;
        case SYSTEM_EVENT_GOT_IP6:
            DEBUG_MSG("IPv6 is preferred");
            break;
        case SYSTEM_EVENT_ETH_START:
            DEBUG_MSG("Ethernet started");
            break;
        case SYSTEM_EVENT_ETH_STOP:
            DEBUG_MSG("Ethernet stopped");
            break;
        case SYSTEM_EVENT_ETH_CONNECTED:
            DEBUG_MSG("Ethernet connected");
            break;
        case SYSTEM_EVENT_ETH_DISCONNECTED:
            DEBUG_MSG("Ethernet disconnected");
            break;
        case SYSTEM_EVENT_ETH_GOT_IP:
            DEBUG_MSG("Obtained IP address");
            break;
        default: break;
    }
}

void reconnectWiFi() {
    const char *wifiName = radioConfig.preferences.wifi_ssid;
    const char *wifiPsw = radioConfig.preferences.wifi_password;

    if (radioConfig.has_preferences) {

        if (*wifiName) {

            DEBUG_MSG("... Reconnecting to WiFi access point");

            WiFi.mode(WIFI_MODE_STA);
            WiFi.begin(wifiName, wifiPsw);
        }
    }
}
