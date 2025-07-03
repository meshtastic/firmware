#include "configuration.h"
#if HAS_WIFI
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "mesh/wifi/WiFiAPClient.h"

#include "main.h"
#include "mesh/api/WiFiServerAPI.h"
#include "target_specific.h"
#include <WiFi.h>

#if HAS_ETHERNET && defined(USE_WS5500)
#include <ETHClass2.h>
#define ETH ETH2
#endif // HAS_ETHERNET

#include <WiFiUdp.h>
#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "mesh/http/WebServer.h"
#endif
#include <ESPmDNS.h>
#include <esp_wifi.h>
static void WiFiEvent(WiFiEvent_t event);
#elif defined(ARCH_RP2040)
#include <SimpleMDNS.h>
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

// To replace blocking wifi connect delay with a non-blocking sleep
static unsigned long wifiReconnectStartMillis = 0;
static bool wifiReconnectPending = false;

bool APStartupComplete = 0;

unsigned long lastrun_ntp = 0;

bool needReconnect = true;   // If we create our reconnector, run it once at the beginning
bool isReconnecting = false; // If we are currently reconnecting

WiFiUDP syslogClient;
Syslog syslog(syslogClient);

Periodic *wifiReconnect;

#ifdef USE_WS5500
// Startup Ethernet
bool initEthernet()
{
    if ((config.network.eth_enabled) && (ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, SPI3_HOST,
                                                   ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN))) {
        WiFi.onEvent(WiFiEvent);
#if !MESHTASTIC_EXCLUDE_WEBSERVER
        createSSLCert(); // For WebServer
#endif
        return true;
    }

    return false;
}
#endif

static void onNetworkConnected()
{
    if (!APStartupComplete) {
        // Start web server
        LOG_INFO("Start network services");

        // start mdns
        if (!MDNS.begin("Meshtastic")) {
            LOG_ERROR("Error setting up mDNS responder!");
        } else {
            LOG_INFO("mDNS Host: Meshtastic.local");
            MDNS.addService("meshtastic", "tcp", SERVER_API_DEFAULT_PORT);
// ESPmDNS (ESP32) and SimpleMDNS (RP2040) have slightly different APIs for adding TXT records
#ifdef ARCH_ESP32
            MDNS.addServiceTxt("meshtastic", "tcp", "shortname", String(owner.short_name));
            MDNS.addServiceTxt("meshtastic", "tcp", "id", String(owner.id));
            // ESP32 prints obtained IP address in WiFiEvent
#elif defined(ARCH_RP2040)
            MDNS.addServiceTxt("meshtastic", "shortname", owner.short_name);
            MDNS.addServiceTxt("meshtastic", "id", owner.id);
            LOG_INFO("Obtained IP address: %s", WiFi.localIP().toString().c_str());
#endif
        }

#ifndef DISABLE_NTP
        LOG_INFO("Start NTP time client");
        timeClient.begin();
        timeClient.setUpdateInterval(60 * 60); // Update once an hour
#endif

        if (config.network.rsyslog_server[0]) {
            LOG_INFO("Start Syslog client");
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
        if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
            initWebServer();
        }
#endif
#if !MESHTASTIC_EXCLUDE_SOCKETAPI
        if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
            initApiServer();
        }
#endif
        APStartupComplete = true;
    }

#if HAS_UDP_MULTICAST
    if (udpHandler && config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST) {
        udpHandler->start();
    }
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
#elif defined(ARCH_RP2040)
        WiFi.disconnect(false);
#endif
        LOG_INFO("Reconnecting to WiFi access point %s", wifiName);

        // Start the non-blocking wait for 5 seconds
        wifiReconnectStartMillis = millis();
        wifiReconnectPending = true;
        // Do not attempt to connect yet, wait for the next invocation
        return 5000; // Schedule next check soon
    }

    // Check if we are ready to proceed with the WiFi connection after the 5s wait
    if (wifiReconnectPending) {
        if (millis() - wifiReconnectStartMillis >= 5000) {
            if (!WiFi.isConnected()) {
#ifdef CONFIG_IDF_TARGET_ESP32C3
                WiFi.mode(WIFI_MODE_NULL);
                WiFi.useStaticBuffers(true);
                WiFi.mode(WIFI_STA);
#endif
                WiFi.begin(wifiName, wifiPsw);
            }
            isReconnecting = false;
            wifiReconnectPending = false;
        } else {
            // Still waiting for 5s to elapse
            return 100; // Check again soon
        }
    }

#ifndef DISABLE_NTP
    if (WiFi.isConnected() && (!Throttle::isWithinTimespanMs(lastrun_ntp, 43200000) || (lastrun_ntp == 0))) { // every 12 hours
        LOG_DEBUG("Update NTP time from %s", config.network.ntp_server);
        if (timeClient.update()) {
            LOG_DEBUG("NTP Request Success - Setting RTCQualityNTP if needed");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);
            lastrun_ntp = millis();
        } else {
            LOG_DEBUG("NTP Update failed");
        }
    }
#endif

    if (config.network.wifi_enabled && !WiFi.isConnected()) {
#ifdef ARCH_RP2040 // (ESP32 handles this in WiFiEvent)
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
#ifdef USE_WS5500
    } else if (config.network.eth_enabled) {
        return true;
#endif
    } else {
        return false;
    }
}

// Disable WiFi
void deinitWifi()
{
    LOG_INFO("WiFi deinit");

    if (isWifiAvailable()) {
#ifdef ARCH_ESP32
        WiFi.disconnect(true, false);
#elif defined(ARCH_RP2040)
        WiFi.disconnect(true);
#endif
        WiFi.mode(WIFI_OFF);
        LOG_INFO("WiFi Turned Off");
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
        WiFi.persistent(false); // Disable flash storage for WiFi credentials
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
#ifdef ARCH_ESP32
                WiFi.config(config.network.ipv4_config.ip, config.network.ipv4_config.gateway, config.network.ipv4_config.subnet,
                            config.network.ipv4_config.dns);
#elif defined(ARCH_RP2040)
                WiFi.config(config.network.ipv4_config.ip, config.network.ipv4_config.dns, config.network.ipv4_config.gateway,
                            config.network.ipv4_config.subnet);
#endif
            }
#ifdef ARCH_ESP32
            WiFi.onEvent(WiFiEvent);
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);

            // This is needed to improve performance.
            esp_wifi_set_ps(WIFI_PS_NONE); // Disable radio power saving

            WiFi.onEvent(
                [](WiFiEvent_t event, WiFiEventInfo_t info) {
                    LOG_WARN("WiFi lost connection. Reason: %d", info.wifi_sta_disconnected.reason);

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
            LOG_DEBUG("JOINING WIFI soon: ssid=%s", wifiName);
            wifiReconnect = new Periodic("WifiConnect", reconnectWiFi);
        }
        return true;
    } else {
        LOG_INFO("Not using WIFI");
        return false;
    }
}

#ifdef ARCH_ESP32
// Called by the Espressif SDK to
static void WiFiEvent(WiFiEvent_t event)
{
    LOG_DEBUG("Network-Event %d: ", event);

    switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
        LOG_INFO("WiFi interface ready");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        LOG_INFO("Completed scan for access points");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        LOG_INFO("WiFi station started");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        LOG_INFO("WiFi station stopped");
        syslog.disable();
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOG_INFO("Connected to access point");
#ifdef WIFI_LED
        digitalWrite(WIFI_LED, HIGH);
#endif
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        LOG_INFO("Disconnected from WiFi access point");
#ifdef WIFI_LED
        digitalWrite(WIFI_LED, LOW);
#endif
        if (!isReconnecting) {
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        LOG_INFO("Authentication mode of access point has changed");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LOG_INFO("Obtained IP address: %s", WiFi.localIP().toString().c_str());
        onNetworkConnected();
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        LOG_INFO("Obtained Local IP6 address: %s", WiFi.linkLocalIPv6().toString().c_str());
        LOG_INFO("Obtained GlobalIP6 address: %s", WiFi.globalIPv6().toString().c_str());
#else
        LOG_INFO("Obtained IP6 address: %s", WiFi.localIPv6().toString().c_str());
#endif
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        LOG_INFO("Lost IP address and IP address is reset to 0");
        if (!isReconnecting) {
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
        }
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        LOG_INFO("WiFi Protected Setup (WPS): succeeded in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        LOG_INFO("WiFi Protected Setup (WPS): failed in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        LOG_INFO("WiFi Protected Setup (WPS): timeout in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        LOG_INFO("WiFi Protected Setup (WPS): pin code in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_PBC_OVERLAP:
        LOG_INFO("WiFi Protected Setup (WPS): push button overlap in enrollee mode");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        LOG_INFO("WiFi access point started");
#ifdef WIFI_LED
        digitalWrite(WIFI_LED, HIGH);
#endif
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        LOG_INFO("WiFi access point stopped");
#ifdef WIFI_LED
        digitalWrite(WIFI_LED, LOW);
#endif
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        LOG_INFO("Client connected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        LOG_INFO("Client disconnected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        LOG_INFO("Assigned IP address to client");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        LOG_INFO("Received probe request");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        LOG_INFO("IPv6 is preferred");
        break;
    case ARDUINO_EVENT_WIFI_FTM_REPORT:
        LOG_INFO("Fast Transition Management report");
        break;
    case ARDUINO_EVENT_ETH_START:
        LOG_INFO("Ethernet started");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        syslog.disable();
        LOG_INFO("Ethernet stopped");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        LOG_INFO("Ethernet connected");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        syslog.disable();
        LOG_INFO("Ethernet disconnected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
#ifdef USE_WS5500
        LOG_INFO("Obtained IP address: %s, %u Mbps, %s", ETH.localIP().toString().c_str(), ETH.linkSpeed(),
                 ETH.fullDuplex() ? "FULL_DUPLEX" : "HALF_DUPLEX");
        onNetworkConnected();
#endif
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
#ifdef USE_WS5500
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        LOG_INFO("Obtained Local IP6 address: %s", ETH.linkLocalIPv6().toString().c_str());
        LOG_INFO("Obtained GlobalIP6 address: %s", ETH.globalIPv6().toString().c_str());
#else
        LOG_INFO("Obtained IP6 address: %s", ETH.localIPv6().toString().c_str());
#endif
#endif
        break;
    case ARDUINO_EVENT_SC_SCAN_DONE:
        LOG_INFO("SmartConfig: Scan done");
        break;
    case ARDUINO_EVENT_SC_FOUND_CHANNEL:
        LOG_INFO("SmartConfig: Found channel");
        break;
    case ARDUINO_EVENT_SC_GOT_SSID_PSWD:
        LOG_INFO("SmartConfig: Got SSID and password");
        break;
    case ARDUINO_EVENT_SC_SEND_ACK_DONE:
        LOG_INFO("SmartConfig: Send ACK done");
        break;
    case ARDUINO_EVENT_PROV_INIT:
        LOG_INFO("Provision Init");
        break;
    case ARDUINO_EVENT_PROV_DEINIT:
        LOG_INFO("Provision Stopped");
        break;
    case ARDUINO_EVENT_PROV_START:
        LOG_INFO("Provision Started");
        break;
    case ARDUINO_EVENT_PROV_END:
        LOG_INFO("Provision End");
        break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
        LOG_INFO("Provision Credentials received");
        break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
        LOG_INFO("Provision Credentials failed");
        break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        LOG_INFO("Provision Credentials success");
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
#endif // HAS_WIFI