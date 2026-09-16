#pragma once

#include "MeshTypes.h"
#include "Observer.h"

/**
 * Delivery state of a reliable (want_ack) packet this node originated.
 *
 * ACKNOWLEDGED carries Meshtastic's existing acknowledgement semantics and nothing more: for a
 * unicast it means the destination (or a relay on its behalf) returned a routing ACK, and for a
 * broadcast it means we overheard another node rebroadcast the packet - evidence that it entered
 * the mesh, not that every node received it.
 */
enum class TxAckState : uint8_t { PENDING, ACKNOWLEDGED, FAILED };

/**
 * One transition of a reliable send we originated. `outstanding` is the authoritative count of our
 * own still-unconfirmed reliable packets taken after the transition, so a consumer can answer "is
 * anything still in flight?" without keeping books of its own.
 */
struct TxAckEvent {
    PacketId id;
    NodeNum to;
    TxAckState state;
    /// The routing error behind a FAILED state, or NONE when the failure carries no
    /// trustworthy routing reason (a locally rejected send - see ReliableRouter::send).
    /// Always NONE for PENDING and ACKNOWLEDGED.
    meshtastic_Routing_Error error;
    uint8_t outstanding;
};

/// Fires on every delivery transition of a reliable packet we originated. Notification is
/// synchronous and one caller is NextHopRouter::doRetransmissions(), mid-iteration over its
/// pending map, so an observer must not originate a packet from the callback - record the
/// transition and act on your own thread, as StatusLEDModule does.
extern Observable<const TxAckEvent *> txAckStatusObservable;
