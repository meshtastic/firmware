#include "mesh/eth/ethClient.h"
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "main.h"
#include "mesh/api/ethServerAPI.h"
#include "target_specific.h"
#include <RAK13800_W5100S.h>
#include <SPI.h>

#if HAS_NETWORKING

#ifndef DISABLE_NTP
#include <NTPClient.h>

// NTP
EthernetUDP ntpUDP;
NTPClient timeClient(ntpUDP, config.network.ntp_server);
uint32_t ntp_renew = 0;
#endif

EthernetUDP syslogClient;
Syslog syslog(syslogClient);

bool ethStartupComplete = 0;

using namespace concurrency;

static Periodic *ethEvent;

static int32_t reconnectETH()
{
    if (config.network.eth_enabled) {
        Ethernet.maintain();
        if (!ethStartupComplete) {
            // Start web server
            LOG_INFO("Start Ethernet network services");

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

#if !MESHTASTIC_EXCLUDE_SOCKETAPI
            initApiServer();
#endif

            ethStartupComplete = true;
        }
    }

#ifndef DISABLE_NTP
    if (isEthernetAvailable() && (ntp_renew < millis())) {

        LOG_INFO("Update NTP time from %s", config.network.ntp_server);
        if (timeClient.update()) {
            LOG_DEBUG("NTP Request Success - Set RTCQualityNTP if needed");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);

            ntp_renew = millis() + 43200 * 1000; // success, refresh every 12 hours
        } else {
            LOG_ERROR("NTP Update failed");
            ntp_renew = millis() + 300 * 1000; // failure, retry every 5 minutes
        }
    }
#endif

    return 5000; // every 5 seconds
}

// Startup Ethernet
bool initEthernet()
{
    if (config.network.eth_enabled) {
#ifdef PIN_ETH_POWER_EN
        pinMode(PIN_ETH_POWER_EN, OUTPUT);
        digitalWrite(PIN_ETH_POWER_EN, HIGH); // Power up.
        delay(100);
#endif

#ifdef PIN_ETHERNET_RESET
        pinMode(PIN_ETHERNET_RESET, OUTPUT);
        digitalWrite(PIN_ETHERNET_RESET, LOW); // Reset Time.
        delay(100);
        digitalWrite(PIN_ETHERNET_RESET, HIGH); // Reset Time.
#endif

#ifdef RAK11310 // Initialize the SPI port
        ETH_SPI_PORT.setSCK(PIN_SPI0_SCK);
        ETH_SPI_PORT.setTX(PIN_SPI0_MOSI);
        ETH_SPI_PORT.setRX(PIN_SPI0_MISO);
        ETH_SPI_PORT.begin();
#endif
        Ethernet.init(ETH_SPI_PORT, PIN_ETHERNET_SS);

        uint8_t mac[6];

        int status = 0;

        //        createSSLCert();

        getMacAddr(mac); // FIXME use the BLE MAC for now...
        mac[0] &= 0xfe;  // Make sure this is not a multicast MAC

        if (config.network.address_mode == meshtastic_Config_NetworkConfig_AddressMode_DHCP) {
            LOG_INFO("Start Ethernet DHCP");
            status = Ethernet.begin(mac);
        } else if (config.network.address_mode == meshtastic_Config_NetworkConfig_AddressMode_STATIC) {
            LOG_INFO("Start Ethernet Static");
            Ethernet.begin(mac, config.network.ipv4_config.ip, config.network.ipv4_config.dns, config.network.ipv4_config.gateway,
                           config.network.ipv4_config.subnet);
            status = 1;
        } else {
            LOG_INFO("Ethernet Disabled");
            return false;
        }

        if (status == 0) {
            if (Ethernet.hardwareStatus() == EthernetNoHardware) {
                LOG_ERROR("Ethernet shield was not found");
                return false;
            } else if (Ethernet.linkStatus() == LinkOFF) {
                LOG_ERROR("Ethernet cable is not connected");
                return false;
            } else {
                LOG_ERROR("Unknown Ethernet error");
                return false;
            }
        } else {
            LOG_INFO("Local IP %u.%u.%u.%u", Ethernet.localIP()[0], Ethernet.localIP()[1], Ethernet.localIP()[2],
                     Ethernet.localIP()[3]);
            LOG_INFO("Subnet Mask %u.%u.%u.%u", Ethernet.subnetMask()[0], Ethernet.subnetMask()[1], Ethernet.subnetMask()[2],
                     Ethernet.subnetMask()[3]);
            LOG_INFO("Gateway IP %u.%u.%u.%u", Ethernet.gatewayIP()[0], Ethernet.gatewayIP()[1], Ethernet.gatewayIP()[2],
                     Ethernet.gatewayIP()[3]);
            LOG_INFO("DNS Server IP %u.%u.%u.%u", Ethernet.dnsServerIP()[0], Ethernet.dnsServerIP()[1], Ethernet.dnsServerIP()[2],
                     Ethernet.dnsServerIP()[3]);
        }

        ethEvent = new Periodic("ethConnect", reconnectETH);

        return true;
    } else {
        LOG_INFO("Not using Ethernet");
        return false;
    }
}

bool isEthernetAvailable()
{

    if (!config.network.eth_enabled) {
        syslog.disable();
        return false;
    } else if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        syslog.disable();
        return false;
    } else if (Ethernet.linkStatus() == LinkOFF) {
        syslog.disable();
        return false;
    } else {
        return true;
    }
}

#endif
