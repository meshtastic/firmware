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
     * PreEncode transport, before the decoded copy is released. Unlike callTransports() this does NOT
     * gate on isEnabled(): the call site applies the transport-specific gate. */
    static void callTransportsPreEncode(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded,
                                        ChannelIndex chIndex);

    /** Drop any queued copy of (from, id) not yet sent, because a duplicate was overheard on
     * `medium`. An overhear is evidence about one medium only, so each transport ignores a cancel
     * for a medium that is not its own. True if any transport dropped something. */
    static bool cancelTransportsOn(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id);

  protected:
    /** True when this transport should receive outgoing packets right now. Only the PostEncode path
     * consults this. */
    virtual bool isEnabled() const = 0;

    /** Queue or emit an outgoing (encrypted) packet. Must not block Router::send(). The return value
     * is ignored: one transport accepting a packet never suppresses another. */
    virtual bool onSend(const meshtastic_MeshPacket *mp) = 0;

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
