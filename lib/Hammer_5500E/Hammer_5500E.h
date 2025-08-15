#ifndef Hammer_5500E_h
#define Hammer_5500E_h

#include <SPI.h>

#define ETHERNET_SCK 35   // sclka
#define ETHERNET_MISO 34  // misoa
#define ETHERNET_MOSI 25  // mosia
#define ETHERNET_CS 16    // ssa

class Hammer_5500E {
public:
    static void begin(uint8_t *mac, IPAddress ip);
    static void begin(uint8_t *mac, IPAddress ip, IPAddress dns);
    static void begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway);
    static void begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet);
    static void init(uint8_t cs_pin = ETHERNET_CS);
private:
    static SPIClass *hspi;
};

#endif