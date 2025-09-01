#include "SerialPacketModule.h"
#include "GeoCoord.h"
#include "MeshService.h"
#include "NMEAWPL.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>
#include <Throttle.h>

/*
    This module implements a serial link for Meshtastic packets.

    This has been tested with the WisMesh starter kit (19007 board+ RAK4630) + RS485 (RAK5802),  CPU is the NRF52840
    This combination uses UART2_RX(P0.15)/UART2_TX(P0.16) on the RAK4630.
    Arduino Serial1 is used in this module for the StreamAPI serial driver
    
    The RS485 serial link is used as an alternative path for packets (similar to mqtt)
     1. Any packet that comes in via wireless is sent out via RS485 (if the packet is rebroadcast)
     2. Any packet that comes in via RS485 serial link is sent out wireless
     3. Any packet that came in via RS485 serial link is never rebroadcast back to the serial link

    A Meshtastic packet sent over the serial link is wrapped in a header with magic numbers and a CRC.
    Incoming packets that fail magic number match or CRC check are discarded.
    This has been tested with RS485 links in excess of 1 km @4800 baud.
    Complete testing results is at: https://github.com/rbreesems/flamingo

    This module does NOT have any module config data yet, so the serialPacketEnabled byte
    below is used for enable/disable, this module is DISABLED by default.
    Also, the module currently uses the Baud rate setting from the serial module.

    You need to be careful of conflicts between this module and the GPS, Serial modules.
    The GPS module for the NRF52840 by default uses Serial1. So, if there is a GPS module, but 
    it does not use the serial uart pins required by this RS485 interface, you could change this code
    to use Serial2.  

    However, the Serial module uses the Serial2 StreamAPI serial driver, so, if you change this code to use Serial2, you 
    would need to disable the serial module (or change the serial module to use Serial1).
    
    If you use this module and the Serial module, be careful that the Serial module UART pin configuration does
    not clash with the pin configuration assumed in this module.

    Assuming that you have two setups of (19007 board+ 4630) + RS485 (RAK5802)  connected together
    (RS485 A wire to A wire, B wire to B wire), the easiest way to test is by doing the following:

     a. Have both Radios configured with Lora Transmit enabled on both.
     b. Connect your phone to Radio1 , verify that a DM to Radio2 is received/acked.
     c. Disable Lora transmit on Radio1.
     d. Send a DM to Radio2 - this should be received/acked and as the packet will go via the RS485 link. If this fails, then
     something is wrong with your serial link -either a wire connection, or a failure in the RS485 module(s)
     e. Assuming success from step (d), disconnect one of the wires
     f. Send a DM to Radio2 - this should time out with max transmissions reached as there is no transmit path for the packet.
     g. Reconnect the wire, and verify that sending another DM to Radio2 works.

*/

#if defined(ARCH_NRF52)

#define TIMEOUT 250
#define BAUD 19200

// defined as UART2 TX/RX on 4630
// This is what is connected on  WisMesh starter kit (19007 board+ 4630) + RS485 (RAK5802)
#define RS485_TXPIN 16
#define RS485_RXPIN 15

#define PACKET_FLAGS_ENCRYPTED_MASK 0x20

SerialPacketModule *serialPacketModule;

meshtastic_serialPacket outPacket;
meshtastic_serialPacket inPacket;
#ifdef SLINK_DEBUG
char tmpbuf[250];  // for debug only
#endif

// since we do not have module config data for this yet, need to put enable byte here
// DISABLED BY DEFAULT
#define SERIAL_PACKET_ENABLED 0
bool serialPacketEnabled = SERIAL_PACKET_ENABLED;

SerialPacketModule::SerialPacketModule() : StreamAPI(&Serial1), concurrency::OSThread("SerialPacket") {}

#define headerByte1 0xaa
#define headerByte2 0x55


size_t serialPacketPayloadSize;

uint32_t computeCrc32(const uint8_t* buf, uint16_t len) {
  uint32_t crc = 0xFFFFFFFF; // Initial value
  const uint32_t poly = 0xEDB88320; // CRC-32 polynomial

  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint8_t)buf[i]; // XOR with the current byte
    for (int j = 7; j >= 0; j--) { // Perform 8 bitwise operations
      if (crc & 0x80000000) { // Check if the MSB is set
        crc = (crc << 1) ^ poly; // Shift and XOR with polynomial
      } else {
        crc <<= 1; // Shift if MSB is not set
      }
    }
  }
  return ~crc; // Return the final CRC value
}


void meshPacketToSerialPacket (const meshtastic_MeshPacket &mp, meshtastic_serialPacket *sp) {
    sp->header.hbyte1 = headerByte1;
    sp->header.hbyte2 = headerByte2;
    sp->header.crc = 0;
    
    if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag ){
        sp->header.size = sizeof(SerialPacketHeader) + mp.encrypted.size;
        memcpy(sp->payload, mp.encrypted.bytes, mp.encrypted.size);
    } else {
        sp->header.size = sizeof(SerialPacketHeader) + mp.decoded.payload.size;
        memcpy(sp->payload, mp.decoded.payload.bytes, mp.decoded.payload.size);
    }
    sp->header.from = mp.from;
    sp->header.to = mp.to;
    sp->header.id = mp.id;
    sp->header.channel = mp.channel;
    
    sp->header.hop_limit = mp.hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK;
    sp->header.hop_start = mp.hop_start & PACKET_FLAGS_HOP_START_MASK;
    sp->header.flags =
        0x00 | 
        (mp.want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) |
        (mp.via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0) |
        ((mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) ? PACKET_FLAGS_ENCRYPTED_MASK : 0);

    sp->header.crc = computeCrc32((const uint8_t *)sp, sp->header.size);
}

void insertSerialPacketToMesh(meshtastic_serialPacket *sp) {

    UniquePacketPoolPacket p = packetPool.allocUniqueZeroed();

    p->from = sp->header.from;
    p->to = sp->header.to;
    p->id = sp->header.id;
    p->channel = sp->header.channel;
    p->hop_limit = sp->header.hop_limit;
    p->hop_start = sp->header.hop_start;
    p->want_ack = !!(sp->header.flags & PACKET_FLAGS_WANT_ACK_MASK);
    p->via_slink = true;
    p->via_mqtt = !!(sp->header.flags & PACKET_FLAGS_VIA_MQTT_MASK);
    uint16_t payloadLen = sp->header.size - sizeof(SerialPacketHeader);
    if (!!(sp->header.flags & PACKET_FLAGS_ENCRYPTED_MASK)) {
        p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        memcpy(p->encrypted.bytes, sp->payload, payloadLen);
        p->encrypted.size = payloadLen;
    } else {
        p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        memcpy(p->decoded.payload.bytes, sp->payload,  payloadLen);
        p->decoded.payload.size = payloadLen;
    }

    LOG_DEBUG ("SerialPacketModule::  RX  from=0x%0x, to=0x%0x, packet_id=0x%0x",
              p->from, p->to, p->id);

#ifdef SLINK_DEBUG
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        memcpy(tmpbuf, p->decoded.payload.bytes, p->decoded.payload.size);
        tmpbuf[p->decoded.payload.size+1]=0;
        LOG_DEBUG("SerialPacketModule::  RX packet of %d bytes, msg: %s", sp->header.size, tmpbuf);
    }
#endif             
    router->enqueueReceivedMessage(p.release());

}


// check if this recieved serial packet is valid
bool checkIfValidSerialPacket(meshtastic_serialPacket *sp) {

    if (sp->header.hbyte1 != headerByte1 || sp->header.hbyte2 != headerByte2 ) {
        LOG_DEBUG("SerialPacketModule:: valid packet check fail, header bytes");
        return false;
    }
    if (sp->header.size == 0 || sp->header.size > sizeof(meshtastic_serialPacket)) {
        LOG_DEBUG("SerialPacketModule:: valid packet check fail, invalid size");
        return false;
    }
    
    uint32_t received_crc = sp->header.crc;
    sp->header.crc = 0; // need to set to zero for computing CRC
    if (computeCrc32((const uint8_t *)sp, sp->header.size) != received_crc) {
        LOG_DEBUG("SerialPacketModule:: valid packet check fail, invalid crc");
        sp->header.crc = received_crc; // restore
        return false;
    }
    sp->header.crc = received_crc; // restore
    return true;
}


int32_t SerialPacketModule::runOnce()
{

    if (!serialPacketEnabled)
        return disable();

    if (firstTime) {
        // Interface with the serial peripheral from in here.
        LOG_INFO("SerialPacketModule:: Init serial interface");

        uint32_t baud = getBaudRate();
        Serial1.setPins(RS485_RXPIN, RS485_TXPIN);
        Serial1.begin(baud, SERIAL_8N1);
        Serial1.setTimeout(moduleConfig.serial.timeout > 0 ? moduleConfig.serial.timeout : TIMEOUT);
        firstTime = 0;
    } else {
            //stream.cpp/readBytes  arduinofruit library
            while (Serial1.available()) {
                serialPacketPayloadSize = Serial1.readBytes((uint8_t *) &inPacket, sizeof(meshtastic_serialPacket));
                 if (!checkIfValidSerialPacket(&inPacket)) {
                    LOG_DEBUG("SerialPacketModule:: failed CRC on RX");
                } else {
                    // checks passed, pass this packet on
                    LOG_DEBUG("SerialPacketModule:: RX Insert packet to mesh");
                    insertSerialPacketToMesh(&inPacket);
                }
            }
        }
    return (50);
} 

/**
 * @brief Checks if the serial connection is established.
 *
 * @return true if the serial connection is established, false otherwise.
 *
 */
bool SerialPacketModule::checkIsConnected()
{
    // we are not going to be able to determine if connected to another radio or not
    // at the end of the serial, just always return true
    return true;  
}

/*
 Called from Router.cpp/Router::send
 Send this over the serial link
*/
void SerialPacketModule::onSend(const meshtastic_MeshPacket &mp) {

    if (mp.via_slink) {
        LOG_DEBUG("SerialPacketModule:: Onsend TX - ignoring packet that came from slink");
    }
    
    LOG_DEBUG("SerialPacketModule:: Onsend TX   from=0x%0x, to=0x%0x, packet_id=0x%0x",
              mp.from, mp.to, mp.id);
    meshPacketToSerialPacket(mp, &outPacket);
    // debug check
    if (!checkIfValidSerialPacket(&outPacket)) {
        LOG_DEBUG("SerialPacketModule:: failed CRC on TX");
    } else {
        if (Serial1.availableForWrite()) {
            LOG_DEBUG("SerialPacketModule:: onSend TX packet of %d bytes", outPacket.header.size);
            Serial1.write((uint8_t *) &outPacket, outPacket.header.size);
        }
    }

}


/**
 * @brief Returns the baud rate of the serial module from the module configuration.
 *
 * @return uint32_t The baud rate of the serial module.
 */
uint32_t SerialPacketModule::getBaudRate()
{
    if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_110) {
        return 110;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_300) {
        return 300;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_600) {
        return 600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_1200) {
        return 1200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_2400) {
        return 2400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_4800) {
        return 4800;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_9600) {
        return 9600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_19200) {
        return 19200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_38400) {
        return 38400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_57600) {
        return 57600;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_115200) {
        return 115200;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_230400) {
        return 230400;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_460800) {
        return 460800;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_576000) {
        return 576000;
    } else if (moduleConfig.serial.baud == meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_921600) {
        return 921600;
    }
    return BAUD;
}


#endif
