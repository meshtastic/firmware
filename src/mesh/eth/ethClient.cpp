#include "mesh/eth/ethClient.h"
#include "NodeDB.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include "target_specific.h"

// Startup Ethernet
bool initEthernet()
{

    config.network.eth_enabled = true;
    config.network.eth_mode = Config_NetworkConfig_EthMode_DHCP;

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

        getMacAddr(mac); // FIXME use the BLE MAC for now...

        if (config.network.eth_mode == Config_NetworkConfig_EthMode_DHCP) {
            DEBUG_MSG("starting Ethernet DHCP\n");
            status = Ethernet.begin(mac);
        } else if (config.network.eth_mode == Config_NetworkConfig_EthMode_STATIC) {
            DEBUG_MSG("starting Ethernet Static\n");
            Ethernet.begin(mac, config.network.eth_config.ip, config.network.eth_config.dns, config.network.eth_config.subnet);
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
