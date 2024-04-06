#pragma once

#include "IClientBase.h"
#include "Packet.h"

class SharedQueue;

/**
 * @brief Client implementation to receive packets from and
 *        send packets to the shared queue
 *
 */
class PacketClient : public IClientBase
{
  public:
    PacketClient();
    void init(void) override;
    bool connect(void) override;
    bool disconnect(void) override;
    bool isConnected(void) override;
    bool send(meshtastic_ToRadio &&to) override;
    meshtastic_FromRadio receive(void) override;

    virtual bool hasData() const;
    virtual bool available() const;

    virtual ~PacketClient() = default;

  protected:
    virtual int connect(SharedQueue *_queue);

  private:
    volatile bool is_connected = false;
    SharedQueue *queue;
};
