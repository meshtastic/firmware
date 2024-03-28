#pragma once

#include "concurrency/PacketQueue.h"
#include "sharedMem/Packet.h"

/**
 * @brief Queue wrapper that aggregates two thread queues (namely client and server)
 *        for bidirectional packet transfer between two threads or processes.
 *
 * This queue may also be created in shared memory (e.g. in Linux for inter-process communication)
 */
class SharedQueue
{
  public:
    SharedQueue();
    virtual ~SharedQueue();

    // server methods
    virtual bool serverSend(Packet &&p);
    virtual Packet::PacketPtr serverReceive();
    virtual size_t serverQueueSize() const;

    // client methods
    virtual bool clientSend(Packet &&p);
    virtual Packet::PacketPtr clientReceive();
    virtual size_t clientQueueSize() const;

  private:
    // the server pushes into serverQueue and the client pushes into clientQueue
    // receiving is done from the opposite queue, respectively
    PacketQueue<Packet> serverQueue;
    PacketQueue<Packet> clientQueue;
};

extern SharedQueue *sharedQueue;