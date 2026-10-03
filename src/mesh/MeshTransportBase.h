#pragma once

#include "mesh/Channels.h" // ChannelIndex, for the pre-encode hook
#include "mesh/MeshTypes.h"
#include <vector>

/**
 * Base class for a non-LoRa transport that Router::send() fans an outgoing packet out to, alongside
 * the mandatory LoRa iface.
 *
 * Registration copies MeshModule's idiom: each instance self-registers in its constructor into a
 * static vector, so adding a transport never touches Router::send(). Unlike MeshModule there is no
 * CONTINUE/STOP contract: these are parallel media, not a handler chain, so every enabled transport
 * gets every packet and each hook's return is ignored.
 *
 * Two fan-out points, because Router::send() reaches them at different packet states:
 *   - PostEncode: the very end, with the final encrypted packet. The broadcast media live here.
 *   - PreEncode: inside the decoded-tag block, after perhapsEncode(), while the decoded copy is
 *     still alive. "PreEncode" names the hook's purpose (acting on decoded content), NOT the packet
 *     state: the packet has already been encrypted by the time it fires.
 * A transport opts into exactly one point via its constructor argument, so the two never cross.
 *
 * Not a RadioInterface: that is the LoRa physical layer. A transport here only needs "is this
 * transport active" and "queue/emit this packet".
 */
class MeshTransportBase
{
  public:
    /** Which of Router::send()'s two fan-out points this transport is invoked from. */
    enum HookPoint { PostEncode, PreEncode };

  private:
    static std::vector<MeshTransportBase *> *postEncodeTransports;
    static std::vector<MeshTransportBase *> *preEncodeTransports;
    const HookPoint hookPoint;

  public:
    explicit MeshTransportBase(HookPoint hook = PostEncode);
    virtual ~MeshTransportBase();

    /** Fans an already-encrypted packet out to every registered PostEncode transport whose
     * isEnabled() is true. Never gates on packet contents; each transport applies its own policy. */
    static void callTransports(const meshtastic_MeshPacket *mp);

    /** Hands the encrypted packet, the decoded copy and the channel index to every registered
     * PreEncode transport whose isEnabled() is true, before the decoded copy is released. */
    static void callTransportsPreEncode(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded,
                                        ChannelIndex chIndex);

    /** Drop any queued copy of (from, id) not yet sent, because a duplicate was overheard on
     * `medium`. An overhear is evidence about one medium only, so each transport ignores a cancel
     * for a medium that is not its own. True if any transport dropped something. */
    static bool cancelTransportsOn(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id);

    /** The ingress rules every non-LoRa bearer shares. False means drop: a local origin or an out-of-range
     * hop count. On true `mp` is stamped with `medium` and its local-only fields are cleared, rx_rssi included. */
    static bool sanitizeIngress(meshtastic_MeshPacket &mp, meshtastic_MeshPacket_TransportMechanism medium);

    /** The packet as it goes onto a non-LoRa bearer, without the local-only fields a receiver overwrites. */
    static meshtastic_MeshPacket stripForTransmit(const meshtastic_MeshPacket &mp);

    /** True when this transport should receive outgoing packets right now. Both fan-out points consult it. */
    virtual bool isEnabled() const = 0;

  protected:
    /** Queue or emit an outgoing (encrypted) packet. Must not block Router::send(). The return value
     * is ignored: one transport accepting a packet never suppresses another. Default no-op, for a
     * PreEncode transport, which is never reached here. */
    virtual bool onSend(const meshtastic_MeshPacket *mp) { return false; }

    /** Act on the decoded packet, with its encrypted copy and channel index, before the decoded copy
     * is freed. Default no-op. Must not block Router::send(). */
    virtual void onSendPreEncode(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded,
                                 ChannelIndex chIndex)
    {
    }

    /** Drop a queued, not-yet-transmitted copy of (from, id) when `medium` is this transport's own.
     * Default no-op: a transport that emits inline has no queue to cancel from. */
    virtual bool onCancelSending(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id) { return false; }
};
