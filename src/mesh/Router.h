#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "PointerQueue.h"
#include "RadioInterface.h"
#include "mesh.pb.h"

/**
 * A mesh aware router that supports multiple interfaces.
 */
class Router
{
  private:
    RadioInterface *iface;

    /// Packets which have just arrived from the radio, ready to be processed by this service and possibly
    /// forwarded to the phone.
    PointerQueue<MeshPacket> fromRadioQueue;

  public:
    /// Local services that want to see _every_ packet this node receives can observe this.
    /// Observers should always return 0 and _copy_ any packets they want to keep for use later (this packet will be getting
    /// freed)
    Observable<const MeshPacket *> notifyPacketReceived;

    /**
     * Constructor
     *
     */
    Router();

    /**
     * Currently we only allow one interface, that may change in the future
     */
    void addInterface(RadioInterface *_iface)
    {
        iface = _iface;
        iface->setReceiver(&fromRadioQueue);
    }

    /**
     * do idle processing
     * Mostly looking in our incoming rxPacket queue and calling handleReceived.
     */
    void loop();

    /**
     * Send a packet on a suitable interface.  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     */
    virtual ErrorCode send(MeshPacket *p);

  protected:
    /**
     * Called from loop()
     * Handle any packet that is received by an interface on this node.
     * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
     *
     * Note: this method will free the provided packet
     */
    virtual void handleReceived(MeshPacket *p);
};

extern Router &router;