#include "mesh/eth/ethClient.h"
#include "NodeDB.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include "target_specific.h"

// Startup Ethernet
bool initEthernet()
{
    if (config.network.eth_enabled) {

        Ethernet.init( SS );

        uint8_t mac[6];

        getMacAddr(mac); // FIXME use the BLE MAC for now...
        
        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
            DEBUG_MSG("Ethernet shield was not found.\n");
        } else if (Ethernet.linkStatus() == LinkOFF) {
            DEBUG_MSG("Ethernet cable is not connected.\n");
        } else if (config.network.eth_mode == Config_NetworkConfig_EthMode_DHCP) {
            DEBUG_MSG("starting Ethernet DHCP\n");
            if (Ethernet.begin(mac) == 0) {
                DEBUG_MSG("DHCP failed\n");
            } else{
                DEBUG_MSG("DHCP assigned IP %s\n",Ethernet.localIP());
            }
        } else if (config.network.eth_mode == Config_NetworkConfig_EthMode_STATIC) {
            DEBUG_MSG("starting Ethernet Static\n");
            Ethernet.begin(mac, config.network.eth_config.ip, config.network.eth_config.dns, config.network.eth_config.subnet);
        } else {
            DEBUG_MSG("Ethernet Disabled\n");
            return false;
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