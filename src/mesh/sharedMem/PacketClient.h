#pragma once

#include "Packet.h"

class SharedQueue;

/**
 * @brief Generic client implementation to receive from and
 *        send packets to the shared queue
 *
 */
class PacketClient
{
  public:
    PacketClient();
    static void init(void);
    virtual int connect(SharedQueue *_queue);
    virtual bool sendPacket(Packet &&p);
    virtual Packet::PacketPtr receivePacket();
    virtual bool hasData() const;
    virtual bool available() const;

  private:
    SharedQueue *queue;
};

extern PacketClient *packetClient;
