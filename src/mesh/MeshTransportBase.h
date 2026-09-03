#pragma once

#include "mesh/MeshTypes.h"
#include <vector>

/**
 * Base class for a non-LoRa broadcast transport that Router::send() fans an outgoing packet out to,
 * alongside the mandatory LoRa iface. Today: UDP multicast and BLE mesh.
 *
 * Registration copies MeshModule's idiom: each instance self-registers in its constructor into a
 * static vector, so adding a transport never touches Router::send(). Unlike MeshModule there is no
 * CONTINUE/STOP contract - these are parallel broadcast media, not a handler chain, so callTransports
 * hands every packet to every enabled transport and ignores each onSend() return.
 *
 * Not a RadioInterface: that is the LoRa physical layer (getPacketTime, ~30 radio members). A
 * transport here only needs "is this transport active" and "queue/emit this packet".
 */
class MeshTransportBase
{
    static std::vector<MeshTransportBase *> *transports;

  public:
    MeshTransportBase();
    virtual ~MeshTransportBase();

    /** Called from Router::send() with a packet that has already been encrypted (or is being
     * relayed already-encrypted). Fans it out to every registered transport whose isEnabled() is
     * true. Never gates on packet contents - each transport applies its own policy in onSend(). */
    static void callTransports(const meshtastic_MeshPacket *mp);

  protected:
    /** True when this transport should receive outgoing packets right now (typically its
     * config.network.enabled_protocols flag). Checked by callTransports before each onSend(). */
    virtual bool isEnabled() const = 0;

    /** Queue or emit an outgoing packet. Must not block Router::send(). The return value is
     * ignored by callTransports - one transport accepting a packet never suppresses another. */
    virtual bool onSend(const meshtastic_MeshPacket *mp) = 0;
};
