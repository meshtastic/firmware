#pragma once

#include "MeshTypes.h"

class RadioInterface;

/**
 * A module's hook into the radio driver's per-packet TX lifecycle.
 *
 * The driver knows this interface and nothing about who implements it: a module needing per-packet
 * radio state (MeshBeacon's preset switch) subclasses this, and one instance registers itself for
 * the life of the program.
 */
class RadioTxHook
{
    friend class RadioTxHooks;

    RadioTxHook *nextHook = nullptr;
    static RadioTxHook *hookList;

  public:
    /// What the driver should do with the packet at the head of the TX queue.
    enum PreTxAction {
        PRETX_SEND,  ///< nothing pending, transmit as usual
        PRETX_DEFER, ///< the radio config changed, re-run the transmit delay before sending
        PRETX_DROP   ///< this packet must not go out on the current radio config
    };

    RadioTxHook();
    virtual ~RadioTxHook();

    /// The driver is about to transmit p (NULL when the queue is empty): set up any state it needs.
    virtual PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p) { return PRETX_SEND; }

    /// True while p needs the radio left on its own config, so the driver must not listen instead.
    virtual bool holdsRadio(const meshtastic_MeshPacket *p) { return false; }

    /// The driver is done with p - sent, cancelled or dropped. Release anything held for it.
    virtual void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p) {}
};

/// Driver-side fan-out over the registered hooks; every call is a no-op when none are registered.
class RadioTxHooks
{
  public:
    /// The first hook not returning PRETX_SEND decides, and the rest are not consulted.
    static RadioTxHook::PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p);
    static bool holdsRadio(const meshtastic_MeshPacket *p);
    static void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p);
};
