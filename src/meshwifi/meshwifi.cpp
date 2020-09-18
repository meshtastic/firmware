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
    /* 
        Note from Jm (Sept 16, 2020):
    
        A bug in the ESP32 SDK was introduced in Oct 2019 that keeps the WiFi radio from
        turning back on after it's shut off. See:
            https://github.com/espressif/arduino-esp32/issues/3522

        Until then, WiFi should only be allowed when there's no power
        saving on the 2.4g transceiver.
    */     

    WiFi.mode(WIFI_MODE_NULL);
    DEBUG_MSG("WiFi Turned Off\n");
    WiFi.printDiag(Serial);
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
                //esp_wifi_set_ps(WIFI_PS_NONE); // Disable power saving


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
            DEBUG_MSG("WiFi interface ready\n");
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            DEBUG_MSG("Completed scan for access points\n");
            break;
        case SYSTEM_EVENT_STA_START:
            DEBUG_MSG("WiFi client started\n");
            break;
        case SYSTEM_EVENT_STA_STOP:
            DEBUG_MSG("WiFi clients stopped\n");
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            DEBUG_MSG("Connected to access point\n");
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            DEBUG_MSG("Disconnected from WiFi access point\n");

            // Reconnect WiFi
            initWifi();
            break;
        case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
            DEBUG_MSG("Authentication mode of access point has changed\n");
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            DEBUG_MSG("Obtained IP address: \n");
            Serial.println(WiFi.localIP());

            // Start web server
            initWebServer();
            
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
            DEBUG_MSG("Obtained IP address\n");
            break;
        default: break;
    }
}