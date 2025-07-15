#include "Hammer_5500E.h"
#include <Ethernet.h>

SPIClass *Hammer_5500E::hspi = nullptr;

void Hammer_5500E::init(uint8_t cs_pin) {
    hspi = new SPIClass(HSPI);
    hspi->begin(ETHERNET_SCK, ETHERNET_MISO, ETHERNET_MOSI, cs_pin);
    Ethernet.init(cs_pin);
}

void Hammer_5500E::begin(uint8_t *mac, IPAddress ip) {
    init(ETHERNET_CS);
    Ethernet.begin(mac, ip);
}

void Hammer_5500E::begin(uint8_t *mac, IPAddress ip, IPAddress dns) {
    init(ETHERNET_CS);
    Ethernet.begin(mac, ip, dns);
}

void Hammer_5500E::begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway) {
    init(ETHERNET_CS);
    Ethernet.begin(mac, ip, dns, gateway);
}

void Hammer_5500E::begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet) {
    init(ETHERNET_CS);
    Ethernet.begin(mac, ip, dns, gateway, subnet);
}