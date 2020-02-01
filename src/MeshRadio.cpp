#include <SPI.h>
#include <RH_RF95.h>
#include <RHMesh.h>
#include <assert.h>

#include "MeshRadio.h"
#include "configuration.h"



// Change to 434.0 or other frequency, must match RX's freq!
#define RF95_FREQ 915.0

MeshRadio radio;

/**
 * get our starting (provisional) nodenum from flash.  But check first if anyone else is using it, by trying to send a message to it (arping)
 */
NodeNum getDesiredNodeNum() {
    uint8_t dmac[6];
    esp_efuse_mac_get_default(dmac);

    // FIXME
    uint8_t r = dmac[5];
    assert(r != 0xff); // It better not be the broadcast address
    return r;
}


MeshRadio::MeshRadio() : rf95(NSS_GPIO, DIO0_GPIO), manager(rf95, getDesiredNodeNum()) {

}

bool MeshRadio::init() {
    pinMode(RESET_GPIO, OUTPUT); // Deassert reset
    digitalWrite(RESET_GPIO, HIGH);

    // pulse reset
    digitalWrite(RESET_GPIO, LOW);
    delay(10);
    digitalWrite(RESET_GPIO, HIGH);
    delay(10);

    if (!manager.init()) {
        Serial.println("LoRa radio init failed");
        Serial.println("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
        return false;
    }

    Serial.println("LoRa radio init OK!");

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
    if (!rf95.setFrequency(RF95_FREQ)) {
        Serial.println("setFrequency failed");
        while (1);
    }
    Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);
    
    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

    // The default transmitter power is 13dBm, using PA_BOOST.
    // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
    // you can set transmitter powers from 5 to 23 dBm:
    // FIXME - can we do this?
    // rf95.setTxPower(23, false);
}


ErrorCode MeshRadio::sendTo(NodeNum dest, const uint8_t *buf, size_t len) {
    return manager.sendtoWait((uint8_t *) buf, len, dest);
}

void MeshRadio::loop() {
    // FIXME read from radio with recvfromAckTimeout
}

void mesh_init() {
  while (!radio.init()) {
    Serial.println("radio init failed");
    while (1);
  }
}


int16_t packetnum = 0;  // packet counter, we increment per xmission

void mesh_loop()
{
  radio.loop();

  delay(1000); // Wait 1 second between transmits, could also 'sleep' here!
  Serial.println("Transmitting..."); // Send a message to rf95_server
  
  char radiopacket[20] = "Hello World #      ";
  sprintf(radiopacket, "hello %d", packetnum++);
  
  Serial.println("Sending...");
  radio.sendTo(NODENUM_BROADCAST, (uint8_t *)radiopacket, sizeof(radiopacket));
}
