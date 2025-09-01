#pragma once

#include "MeshModule.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#if defined(ARCH_NRF52)

extern bool serialPacketEnabled;

/** 
 * Header for wrapper around Meshtastic packet data sent over the serial link
**/
typedef struct _SerialPacketHeader{
    uint8_t hbyte1; //magic number for early rejection
    uint8_t hbyte2; //magic number for early rejection
    uint16_t size;  //this is size of header + payload length
    uint32_t crc;
    NodeNum to, from; // can be 1 byte or four bytes
    PacketId id; // can be 1 byte or 4 bytes

    /**
     * This flag bytes holds 3 flags from original Meshtastic flags - want_ack, via_mqtt, is_encrypted
     **/
    uint8_t flags;

    /** The channel hash - used as a hint for the decoder to limit which channels we consider */
    uint8_t channel;
    uint8_t hop_limit;   
    uint8_t hop_start;   

    
} SerialPacketHeader;


typedef struct _meshtastic_serialPacket{
    SerialPacketHeader header;
    uint8_t payload[256];    // 256 is max payload size
} meshtastic_serialPacket;



class SerialPacketModule : public StreamAPI, private concurrency::OSThread
{
    bool firstTime = 1;
    
  public:
    SerialPacketModule();
    void onSend(const meshtastic_MeshPacket &mp);

  protected:
    virtual int32_t runOnce() override;

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;
   
  private:
    uint32_t getBaudRate();
    
};

extern SerialPacketModule *serialPacketModule;


#endif
