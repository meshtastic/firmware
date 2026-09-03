#pragma once

#include "mesh/Channels.h" // ChannelIndex, for the pre-encode hook
#include "mesh/MeshTypes.h"
#include <vector>

/**
 * Base class for a non-LoRa transport that Router::send() fans an outgoing packet out to, alongside
 * the mandatory LoRa iface. Today: UDP multicast and BLE mesh (post-encode) and MQTT (pre-encode).
 *
 * Registration copies MeshModule's idiom: each instance self-registers in its constructor into a
 * static vector, so adding a transport never touches Router::send(). Unlike MeshModule there is no
 * CONTINUE/STOP contract - these are parallel media, not a handler chain, so a call hands every packet
 * to every enabled transport and ignores each hook's return.
 *
 * There are two fan-out points because Router::send() reaches them at different packet states:
 *   - PostEncode: the very end, with the final (already-encrypted, or relayed-encrypted) packet. The
 *     broadcast media (UDP, BLE) live here - they re-emit exactly what LoRa would.
 *   - PreEncode: inside the decoded-tag block, after perhapsEncode(), while the decoded copy is still
 *     alive. MQTT lives here because it needs the decoded packet plus the channel index. "PreEncode"
 *     names the hook's purpose (acting on decoded content), NOT the packet state: the packet has
 *     already been encrypted by the time this fires - callTransportsPreEncode still receives it.
 * A transport opts into exactly one point via its constructor argument, so the two never cross: a
 * PreEncode transport is invisible to callTransports(), and vice versa.
 *
 * Not a RadioInterface: that is the LoRa physical layer (getPacketTime, ~30 radio members). A
 * transport here only needs "is this transport active" and "queue/emit this packet".
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

    /** Called from Router::send() with a packet that has already been encrypted (or is being relayed
     * already-encrypted). Fans it out to every registered PostEncode transport whose isEnabled() is
     * true. Never gates on packet contents - each transport applies its own policy in onSend(). */
    static void callTransports(const meshtastic_MeshPacket *mp);

    /** Called from Router::send() inside the decoded-tag block, after perhapsEncode() and before the
     * decoded copy is released. Hands both the now-encrypted packet and the decoded copy (plus the
     * channel index) to every registered PreEncode transport. Unlike callTransports() this does NOT
     * gate on isEnabled(): the caller applies the transport-specific gate (e.g. moduleConfig.mqtt.enabled
     * && isFromUs) at the call site, and each transport applies the rest of its policy inside its hook. */
    static void callTransportsPreEncode(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded,
                                        ChannelIndex chIndex);

  protected:
    /** True when this transport should receive outgoing packets right now (typically its
     * config.network.enabled_protocols flag). Checked by callTransports before each onSend(). Only the
     * PostEncode path consults this. */
    virtual bool isEnabled() const = 0;

    /** Queue or emit an outgoing (encrypted) packet. Must not block Router::send(). The return value is
     * ignored by callTransports - one transport accepting a packet never suppresses another. Only the
     * PostEncode path calls this. */
    virtual bool onSend(const meshtastic_MeshPacket *mp) = 0;

    /** Pre-encode hook: act on the decoded packet (with its encrypted copy and channel index) before the
     * decoded copy is freed. Default no-op, so a PostEncode transport never sees it. A PreEncode transport
     * overrides this and applies its own policy inside. Must not block Router::send(). */
    virtual void onSendPreEncode(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded,
                                 ChannelIndex chIndex)
    {
    }
};
