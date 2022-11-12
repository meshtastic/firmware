#include "mesh/eth/ethClient.h"
#include "NodeDB.h"
#include "RTC.h"
#include "concurrency/Periodic.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include "target_specific.h"
#include "mesh/eth/ethServerAPI.h"
#include "mqtt/MQTT.h"

#ifndef DISABLE_NTP
#include <NTPClient.h>

// NTP
EthernetUDP ntpUDP;
NTPClient timeClient(ntpUDP, config.network.ntp_server);
uint32_t ntp_renew = 0;
#endif

bool ethStartupComplete = 0;

using namespace concurrency;

static Periodic *ethEvent;

static int32_t reconnectETH()
{
    if (config.network.eth_enabled) {
        Ethernet.maintain();
        if (!ethStartupComplete) {
            // Start web server
            DEBUG_MSG("... Starting network services\n");

#ifndef DISABLE_NTP
            DEBUG_MSG("Starting NTP time client\n");
            timeClient.begin();
            timeClient.setUpdateInterval(60 * 60); // Update once an hour
#endif            
            // initWebServer();
            initApiServer();

            ethStartupComplete = true;
        }

        // FIXME this is kinda yucky, instead we should just have an observable for 'wifireconnected'
        if (mqtt && !mqtt->connected()) {
            mqtt->reconnect();
        }
    }

#ifndef DISABLE_NTP
    if (isEthernetAvailable() && (ntp_renew < millis())) {
	    
        DEBUG_MSG("Updating NTP time from %s\n", config.network.ntp_server);
        if (timeClient.update()) {
            DEBUG_MSG("NTP Request Success - Setting RTCQualityNTP if needed\n");

            struct timeval tv;
            tv.tv_sec = timeClient.getEpochTime();
            tv.tv_usec = 0;

            perhapsSetRTC(RTCQualityNTP, &tv);

            ntp_renew = millis() + 43200 * 1000; // success, refresh every 12 hours

        } else {
            DEBUG_MSG("NTP Update failed\n");
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

#ifdef PIN_ETHERNET_RESET
        pinMode(PIN_ETHERNET_RESET, OUTPUT);
        digitalWrite(PIN_ETHERNET_RESET, LOW);  // Reset Time.
        delay(100);
        digitalWrite(PIN_ETHERNET_RESET, HIGH);  // Reset Time.
#endif

        Ethernet.init( ETH_SPI_PORT, PIN_ETHERNET_SS );

        uint8_t mac[6];

        int status = 0;

        //        createSSLCert();

        getMacAddr(mac); // FIXME use the BLE MAC for now...

        if (config.network.eth_mode == Config_NetworkConfig_EthMode_DHCP) {
            DEBUG_MSG("starting Ethernet DHCP\n");
            status = Ethernet.begin(mac);
        } else if (config.network.eth_mode == Config_NetworkConfig_EthMode_STATIC) {
            DEBUG_MSG("starting Ethernet Static\n");
            Ethernet.begin(mac, config.network.ipv4_config.ip, config.network.ipv4_config.dns, config.network.ipv4_config.subnet);
        } else {
            DEBUG_MSG("Ethernet Disabled\n");
            return false;
        }

        if (status == 0) {
            if (Ethernet.hardwareStatus() == EthernetNoHardware) {
                DEBUG_MSG("Ethernet shield was not found.\n");
                return false;
            } else if (Ethernet.linkStatus() == LinkOFF) {
                DEBUG_MSG("Ethernet cable is not connected.\n");
                return false;
            } else{
                DEBUG_MSG("Unknown Ethernet error.\n");
                return false;
            }
        } else {
            DEBUG_MSG("Local IP %u.%u.%u.%u\n",Ethernet.localIP()[0], Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3]);
            DEBUG_MSG("Subnet Mask %u.%u.%u.%u\n",Ethernet.subnetMask()[0], Ethernet.subnetMask()[1], Ethernet.subnetMask()[2], Ethernet.subnetMask()[3]);
            DEBUG_MSG("Gateway IP %u.%u.%u.%u\n",Ethernet.gatewayIP()[0], Ethernet.gatewayIP()[1], Ethernet.gatewayIP()[2], Ethernet.gatewayIP()[3]);
            DEBUG_MSG("DNS Server IP %u.%u.%u.%u\n",Ethernet.dnsServerIP()[0], Ethernet.dnsServerIP()[1], Ethernet.dnsServerIP()[2], Ethernet.dnsServerIP()[3]);
        }

        ethEvent = new Periodic("ethConnect", reconnectETH);

        return true;

    } else {
        DEBUG_MSG("Not using Ethernet\n");
        return false;
    }
}

bool isEthernetAvailable() {

    if (!config.network.eth_enabled) {
        return false;
    } else if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        return false;
    } else if (Ethernet.linkStatus() == LinkOFF) {
        return false;
    } else {
        return true;
    }
}
