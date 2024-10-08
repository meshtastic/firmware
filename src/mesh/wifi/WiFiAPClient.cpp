#include "configuration.h"
#if HAS_WIFI
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "mesh/wifi/WiFiAPClient.h"

#include "main.h"
#include "mesh/api/WiFiServerAPI.h"
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "target_specific.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "mesh/http/WebServer.h"
#endif
#include <ESPmDNS.h>
#include <esp_wifi.h>
static void WiFiEvent(WiFiEvent_t event);
#endif

#ifndef DISABLE_NTP
#include "Throttle.h"
#include <NTPClient.h>
#endif

using namespace concurrency;

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

bool needReconnect = true;   // If we create our reconnector, run it once at the beginning
bool isReconnecting = false; // If we are currently reconnecting

WiFiUDP syslogClient;
Syslog syslog(syslogClient);

Periodic *wifiReconnect;

static void onNetworkConnected()
{
    if (!APStartupComplete) {
        // Start web server
        LOG_INFO("Starting WiFi network services\n");

#ifdef ARCH_ESP32
        // start mdns
        if (!MDNS.begin("Meshtastic")) {
            LOG_ERROR("Error setting up MDNS responder!\n");
        } else {
            LOG_INFO("mDNS responder started\n");
            LOG_INFO("mDNS Host: Meshtastic.local\n");
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("https", "tcp", 443);
        }
#else // ESP32 handles this in WiFiEvent
        LOG_INFO("Obtained IP address: %s\n", WiFi.localIP().toString().c_str());
#endif

#ifndef DISABLE_NTP
        LOG_INFO("Starting NTP time client\n");
        timeClient.begin();
        timeClient.setUpdateInterval(60 * 60); // Update once an hour
#endif

        if (config.network.rsyslog_server[0]) {
            LOG_INFO("Starting Syslog client\n");
            // Defaults
            int serverPort = 514;
            const char *serverAddr = config.network.rsyslog_server;
            String server = String(serverAddr);
            int delimIndex = server.indexOf(':');
            if (delimIndex > 0) {
                String port = server.substring(delimIndex + 1, server.length());
                server[delimIndex] = 0;
                serverPort = port.toInt();
                serverAddr = server.c_str();
            }
            syslog.server(serverAddr, serverPort);
            syslog.deviceHostname(getDeviceName());
            syslog.appName("Meshtastic");
            syslog.defaultPriority(LOGLEVEL_USER);
            syslog.enable();
        }

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_WEBSERVER
        initWebServer();
#endif
        initApiServer();
        APStartupComplete = true;
    }

    // FIXME this is kinda yucky, instead we should just have an observable for 'wifireconnected'
#ifndef MESHTASTIC_EXCLUDE_MQTT
    if (mqtt)
        mqtt->reconnect();
#endif
}

static int32_t reconnectWiFi()
{
    const char *wifiName = config.network.wifi_ssid;
    const char *wifiPsw = config.network.wifi_psk;

    if (config.network.wifi_enabled && needReconnect) {

        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        needReconnect = false;
        isReconnecting = true;

        // Make sure we clear old connection credentials
#ifdef ARCH_ESP32
        WiFi.disconnect(false, true);
#else
        WiFi.disconnect(false);
#endif
        LOG_INFO("Reconnecting to WiFi access point %s\n", wifiName);

        delay(5000);

        if (!WiFi.isConnected()) {
            WiFi.begin(wifiName, wifiPsw);
        }
        isReconnecting = false;
    }

#ifndef DISABLE_NTP
    if (WiFi.isConnected() && (!Throttle::isWithinTimespanMs(lastrun_ntp, 43200000) || (lastrun_ntp == 0))) { // every 12 hours
        LOG_DEBUG("Updating NTP time from %s\n", config.network.ntp_server);
        if (timeClient.update()) {
            LOG_DEBUG("NTP Request Success - Setting RTCQualityNTP if needed\n");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);
            lastrun_ntp = millis();
        } else {
            LOG_DEBUG("NTP Update failed\n");
        }
    }
#endif

    if (config.network.wifi_enabled && !WiFi.isConnected()) {
#ifdef ARCH_RP2040 // (ESP32 handles this in WiFiEvent)
        /* If APStartupComplete, but we're not connected, try again.
           Shouldn't try again before APStartupComplete. */
        needReconnect = APStartupComplete;
#endif
        return 1000; // check once per second
    } else {
#ifdef ARCH_RP2040
        onNetworkConnected(); // will only do anything once
#endif
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
    LOG_INFO("WiFi deinit\n");

    if (isWifiAvailable()) {
#ifdef ARCH_ESP32
        WiFi.disconnect(true, false);
#else
        WiFi.disconnect(true);
#endif
        WiFi.mode(WIFI_OFF);
        LOG_INFO("WiFi Turned Off\n");
        // WiFi.printDiag(Serial);
    }
}

// Startup WiFi
bool initWifi()
{
    if (config.network.wifi_enabled && config.network.wifi_ssid[0]) {

        const char *wifiName = config.network.wifi_ssid;
        const char *wifiPsw = config.network.wifi_psk;

#ifndef ARCH_RP2040
#if !MESHTASTIC_EXCLUDE_WEBSERVER
        createSSLCert(); // For WebServer
#endif
        esp_wifi_set_storage(WIFI_STORAGE_RAM); // Disable flash storage for WiFi credentials
#endif
        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;

        if (*wifiName) {
            uint8_t dmac[6];
            getMacAddr(dmac);
            snprintf(ourHost, sizeof(ourHost), "Meshtastic-%02x%02x", dmac[4], dmac[5]);

            WiFi.mode(WIFI_STA);
            WiFi.setHostname(ourHost);

            if (config.network.address_mode == meshtastic_Config_NetworkConfig_AddressMode_STATIC &&
                config.network.ipv4_config.ip != 0) {
#ifndef ARCH_RP2040
                WiFi.config(config.network.ipv4_config.ip, config.network.ipv4_config.gateway, config.network.ipv4_config.subnet,
                            config.network.ipv4_config.dns);
#else
                WiFi.config(config.network.ipv4_config.ip, config.network.ipv4_config.dns, config.network.ipv4_config.gateway,
                            config.network.ipv4_config.subnet);
#endif
            }
#ifndef ARCH_RP2040
            WiFi.onEvent(WiFiEvent);
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);

            // This is needed to improve performance.
            esp_wifi_set_ps(WIFI_PS_NONE); // Disable radio power saving

            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    LOG_WARN("WiFi lost connection. Reason: %d\n", info.wifi_sta_disconnected.reason);

                    /*
                        If we are disconnected from the AP for some reason,
                        save the error code.

                        For a reference to the codes:
                            https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
                    */
                    wifiDisconnectReason = info.wifi_sta_disconnected.reason;
                },
                WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif
            LOG_DEBUG("JOINING WIFI soon: ssid=%s\n", wifiName);
            wifiReconnect = new Periodic("WifiConnect", reconnectWiFi);
        }
        return true;
    } else {
        LOG_INFO("Not using WIFI\n");
        return false;
    }
}

#ifdef ARCH_ESP32
// Called by the Espressif SDK to
static void WiFiEvent(WiFiEvent_t event)
{
    LOG_DEBUG("WiFi-Event %d: ", event);

    switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
        LOG_INFO("WiFi interface ready\n");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        LOG_INFO("Completed scan for access points\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        LOG_INFO("WiFi station started\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        LOG_INFO("WiFi station stopped\n");
        syslog.disable();
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOG_INFO("Connected to access point\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        LOG_INFO("Disconnected from WiFi access point\n");
        if (!isReconnecting) {
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        LOG_INFO("Authentication mode of access point has changed\n");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LOG_INFO("Obtained IP address: %s\n", WiFi.localIP().toString().c_str());
        onNetworkConnected();
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        LOG_INFO("Obtained Local IP6 address: %s\n", WiFi.linkLocalIPv6().toString().c_str());
        LOG_INFO("Obtained GlobalIP6 address: %s\n", WiFi.globalIPv6().toString().c_str());
#else
        LOG_INFO("Obtained IP6 address: %s\n", WiFi.localIPv6().toString().c_str());
#endif
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        LOG_INFO("Lost IP address and IP address is reset to 0\n");
        if (!isReconnecting) {
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
        }
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        LOG_INFO("WiFi Protected Setup (WPS): succeeded in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        LOG_INFO("WiFi Protected Setup (WPS): failed in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        LOG_INFO("WiFi Protected Setup (WPS): timeout in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        LOG_INFO("WiFi Protected Setup (WPS): pin code in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WPS_ER_PBC_OVERLAP:
        LOG_INFO("WiFi Protected Setup (WPS): push button overlap in enrollee mode\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        LOG_INFO("WiFi access point started\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        LOG_INFO("WiFi access point stopped\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        LOG_INFO("Client connected\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        LOG_INFO("Client disconnected\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        LOG_INFO("Assigned IP address to client\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        LOG_INFO("Received probe request\n");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        LOG_INFO("IPv6 is preferred\n");
        break;
    case ARDUINO_EVENT_WIFI_FTM_REPORT:
        LOG_INFO("Fast Transition Management report\n");
        break;
    case ARDUINO_EVENT_ETH_START:
        LOG_INFO("Ethernet started\n");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        LOG_INFO("Ethernet stopped\n");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        LOG_INFO("Ethernet connected\n");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        LOG_INFO("Ethernet disconnected\n");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        LOG_INFO("Obtained IP address (ARDUINO_EVENT_ETH_GOT_IP)\n");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        LOG_INFO("Obtained IP6 address (ARDUINO_EVENT_ETH_GOT_IP6)\n");
        break;
    case ARDUINO_EVENT_SC_SCAN_DONE:
        LOG_INFO("SmartConfig: Scan done\n");
        break;
    case ARDUINO_EVENT_SC_FOUND_CHANNEL:
        LOG_INFO("SmartConfig: Found channel\n");
        break;
    case ARDUINO_EVENT_SC_GOT_SSID_PSWD:
        LOG_INFO("SmartConfig: Got SSID and password\n");
        break;
    case ARDUINO_EVENT_SC_SEND_ACK_DONE:
        LOG_INFO("SmartConfig: Send ACK done\n");
        break;
    case ARDUINO_EVENT_PROV_INIT:
        LOG_INFO("Provisioning: Init\n");
        break;
    case ARDUINO_EVENT_PROV_DEINIT:
        LOG_INFO("Provisioning: Stopped\n");
        break;
    case ARDUINO_EVENT_PROV_START:
        LOG_INFO("Provisioning: Started\n");
        break;
    case ARDUINO_EVENT_PROV_END:
        LOG_INFO("Provisioning: End\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
        LOG_INFO("Provisioning: Credentials received\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
        LOG_INFO("Provisioning: Credentials failed\n");
        break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        LOG_INFO("Provisioning: Credentials success\n");
        break;
    default:
        break;
    }
}
#endif

uint8_t getWifiDisconnectReason()
{
    return wifiDisconnectReason;
}
#endif