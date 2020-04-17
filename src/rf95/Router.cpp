#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

/**
 * Router todo
 *
 * DONE: Implement basic interface and use it elsewhere in app
 * Add naive flooding mixin (& drop duplicate rx broadcasts), add tools for sending broadcasts with incrementing sequence #s
 * Add an optional adjacent node only 'send with ack' mixin.  If we timeout waiting for the ack, call handleAckTimeout(packet)
 * Add DSR mixin
 *
 **/

#define MAX_RX_FROMRADIO                                                                                                         \
    4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

// I think this is right, one packet for each of the three fifos + one packet being currently assembled for TX or RX
#define MAX_PACKETS                                                                                                              \
    (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + MAX_TX_QUEUE +                                                                          \
     2) // max number of packets which can be in flight (either queued from reception or queued for sending)

MemoryPool<MeshPacket> packetPool(MAX_PACKETS);

/**
 * Constructor
 *
 * Currently we only allow one interface, that may change in the future
 */
Router::Router() : fromRadioQueue(MAX_RX_FROMRADIO) {}

/**
 * do idle processing
 * Mostly looking in our incoming rxPacket queue and calling handleReceived.
 */
void Router::loop()
{
    MeshPacket *mp;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL) {
        handleReceived(mp);
    }
}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode Router::send(MeshPacket *p)
{
    assert(iface);
    return iface->send(p);
}

#include "GPS.h"

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void Router::handleReceived(MeshPacket *p)
{
    // FIXME, this class shouldn't EVER need to know about the GPS, move getValidTime() into a non gps dependent function
    // Also, we should set the time from the ISR and it should have msec level resolution
    p->rx_time = gps.getValidTime(); // store the arrival timestamp for the phone

    DEBUG_MSG("Notifying observers of received packet\n");
    notifyPacketReceived.notifyObservers(p);
    packetPool.release(p);
}