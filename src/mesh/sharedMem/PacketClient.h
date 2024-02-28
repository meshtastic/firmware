#pragma once

#include "IClientBase.h"
#include "Packet.h"

class SharedQueue;

/**
 * @brief Generic client implementation to receive from and
 *        send packets to the shared queue
 *
 */
class PacketClient : public IClientBase
{
  public:
    PacketClient();
    virtual void init(void);
    virtual bool connect(void);
    virtual bool disconnect(void);
    virtual bool isConnected(void);

    virtual bool sendPacket(Packet &&p);
    virtual Packet::PacketPtr receivePacket();

    virtual bool hasData() const;
    virtual bool available() const;

    virtual ~PacketClient() = default;

  protected:
    virtual int connect(SharedQueue *_queue);

  private:
    bool is_connected = false;
    SharedQueue *queue;
};
