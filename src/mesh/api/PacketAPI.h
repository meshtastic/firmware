#pragma once

#include "PhoneAPI.h"
#include "concurrency/OSThread.h"
#include "sharedMem/PacketServer.h"

/**
 * A version of the phone API used for inter task communication based on protobuf packets, e.g.
 * between two tasks running on CPU0 and CPU1, respectively.
 *
 */
class PacketAPI : public PhoneAPI, public concurrency::OSThread
{
  public:
    PacketAPI(PacketServer *_server);
    static void init(void);
    virtual ~PacketAPI(){};
    virtual int32_t runOnce();

  protected:
    // Check the current underlying physical queue to see if the client is fetching packets
    bool checkIsConnected() override;

    void onNowHasData(uint32_t fromRadioNum) override {}
    void onConnectionChanged(bool connected) override {}

  private:
    bool receivePacket(void);
    bool sendPacket(void);

    bool isConnected;
    PacketServer *server;
    uint8_t txBuf[MAX_TO_FROM_RADIO_SIZE] = {0}; // dummy buf to obey PhoneAPI
};

extern PacketAPI *packetAPI;