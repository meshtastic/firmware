// src/modules/EthernetModule/EthernetModule.h
#ifndef ETHERNET_MODULE_H
#define ETHERNET_MODULE_H
#include "MeshModule.h"
#include <SPI.h>
#include <Ethernet.h>
class EthernetModule : public MeshModule {
public:
    EthernetModule();
    virtual void setup() override;
    void loop();
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
private:
    SPIClass hspi;
    bool initialized;
};
extern EthernetModule ethernetModule;
#endif