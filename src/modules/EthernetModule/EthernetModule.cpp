// src/modules/EthernetModule/EthernetModule.cpp
#include "EthernetModule.h"
#include "configuration.h"
EthernetModule ethernetModule;
EthernetModule::EthernetModule() : MeshModule("EthernetModule"), hspi(HSPI), initialized(false) {
}
void EthernetModule::setup() {
    #define ETH_CS 16
    #define ETH_SCK 35
    #define ETH_MISO 34
    #define ETH_MOSI 25
    #define ETH_RESET 17
    byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // Ensure unique MAC
    IPAddress staticIP(192, 168, 1, 100); // Fallback static IP
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    LOG_INFO("Initializing W5500 Ethernet on HSPI\n");
    hspi.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
    LOG_INFO("HSPI initialized: CS=%d, SCK=%d, MISO=%d, MOSI=%d\n", ETH_CS, ETH_SCK, ETH_MISO, ETH_MOSI);
    pinMode(ETH_RESET, OUTPUT);
    digitalWrite(ETH_RESET, LOW);
    delay(100);
    digitalWrite(ETH_RESET, HIGH);
    delay(100);
    LOG_INFO("Ethernet reset completed\n");
    Ethernet.init(ETH_CS);
    LOG_INFO("Ethernet.init called with CS=%d\n", ETH_CS);
    if (Ethernet.begin(mac) == 0) {
        LOG_ERROR("DHCP failed, trying static IP\n");
        Ethernet.begin(mac, staticIP, gateway, subnet);
        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
            LOG_ERROR("W5500 hardware not found\n");
        } else if (Ethernet.linkStatus() == LinkOFF) {
            LOG_ERROR("Ethernet cable not connected\n");
        } else {
            LOG_INFO("Static IP assigned: %s\n", Ethernet.localIP().toString().c_str());
            initialized = true;
        }
    } else {
        LOG_INFO("DHCP success, IP address: %s\n", Ethernet.localIP().toString().c_str());
        initialized = true;
    }
}
void EthernetModule::loop() {
    if (initialized) {
        Ethernet.maintain();
        LOG_DEBUG("Ethernet link status: %s\n", Ethernet.linkStatus() == LinkON ? "ON" : "OFF");
    }
}
bool EthernetModule::wantPacket(const meshtastic_MeshPacket *p) {
    return false;
}