#pragma once
#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/atak.pb.h"

/**
 * ATAK Plugin V2 module - passthrough for ATAK_PLUGIN_V2 payloads.
 * The wire format includes a leading flags byte followed by opaque payload bytes.
 * Depending on the flags, the payload may be zstd dictionary-compressed or raw/uncompressed protobuf.
 * Compression/decompression and payload interpretation are handled by the apps
 * (Android, iOS, ATAK plugin); firmware forwards the bytes unchanged on the
 * ATAK_PLUGIN_V2 port.
 */
class AtakPluginModule : public SinglePortModule
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AtakPluginModule();

    /// TAK and TAK_TRACKER send on-device text as GeoChat instead of TEXT_MESSAGE_APP
    static bool sendsTextAsGeoChat();

    /// Fill the callsign, team and role every TAKPacketV2 we originate carries
    static void fillSender(meshtastic_TAKPacketV2 &takPacket);

    /// Set p's port and payload to [flags byte][uncompressed TAKPacketV2]. False if it doesn't fit.
    static bool setPayload(meshtastic_MeshPacket *p, const meshtastic_TAKPacketV2 &takPacket);

    /// Replace p's payload with a GeoChat carrying message to p->to, cutting text that doesn't fit the
    /// frame p's channel and pki_encrypted give it. carried, if given, gets the text bytes that went in.
    static bool setGeoChatPayload(meshtastic_MeshPacket *p, const char *message, size_t len, size_t *carried = nullptr);

    /// Longest message (bytes) setGeoChatPayload can carry to dest on channel, PKC-encrypted or not,
    /// without cutting it: what still fits once the router adds PKC, AEAD or a signature
    static size_t maxGeoChatBytes(NodeNum dest, ChannelIndex channel, bool pki);

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern AtakPluginModule *atakPluginModule;
