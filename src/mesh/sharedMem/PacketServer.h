#pragma once

#include "concurrency/PacketQueue.h"
#include "sharedMem/Packet.h"

class SharedQueue;

/**
 * Generic server implementation (base class) for bidirectional task communication
 * Uses a queue that is shared with the
 */
class PacketServer
{
  public:
    PacketServer();
    virtual void begin(SharedQueue *_queue);
    virtual bool sendPacket(Packet &&p);
    virtual Packet::PacketPtr receivePacket();
    // template<typename T> T receivePacket();
    virtual bool hasData() const;
    virtual bool available() const;

  private:
    SharedQueue *queue;
};
