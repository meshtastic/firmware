#include "AtakPluginModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"

extern "C" {
#include "mesh/compression/unishox2.h"
}

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule()
    : ProtobufModule("atak", meshtastic_PortNum_ATAK_PLUGIN, &meshtastic_TAK_Packet_msg),
      concurrency::OSThread("AtakPluginModule")
{
    ourPortNum = meshtastic_PortNum_ATAK_PLUGIN;
}

/*
Encompasses the full construction and sending packet to mesh
Will be used for broadcast.
*/
int32_t AtakPluginModule::runOnce()
{
    return default_broadcast_interval_secs;
}

bool AtakPluginModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_TAK_Packet *r)
{
    return false;
}

void AtakPluginModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAK_Packet *t)
{
    // Not PLI, ignore it
    if (t->which_payload_variant != meshtastic_TAK_Packet_tak_pli_tag)
        return;

    // From Phone (EUD)
    if (mp.from == 0) {
        LOG_DEBUG("Received uncompressed TAK payload from phone with %d bytes\n", mp.decoded.payload.size);
        // Compress for LoRA transport
        meshtastic_TAK_Packet compressed = meshtastic_TAK_Packet_init_default;
        compressed.which_callsign_variant = meshtastic_TAK_Packet_callsign_compressed_tag;
        compressed.which_payload_variant = meshtastic_TAK_Packet_tak_pli_tag;
        compressed.payload_variant.tak_pli = t->payload_variant.tak_pli;
        auto length =
            unishox2_compress_simple(t->callsign_variant.callsign_uncompressed, sizeof(t->callsign_variant.callsign_uncompressed),
                                     compressed.callsign_variant.callsign_compressed);
        LOG_DEBUG("Unompressed callsign %d bytes\n", strlen(t->callsign_variant.callsign_uncompressed));
        LOG_DEBUG("Compressed callsign %d bytes\n", length);
        mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                     meshtastic_TAK_Packet_fields, &compressed);
        LOG_DEBUG("Final payload size of %d bytes\n", mp.decoded.payload.size);
    } else {
        if (t->which_callsign_variant != meshtastic_TAK_Packet_callsign_compressed_tag) {
            // Not compressed. Something
            LOG_ERROR("Received uncompressed TAK_Packet atak plugin msg!\n");
            return;
        }
        // From another node on the mesh
        // Decompress for Phone (EUD)
        auto decompressedCopy = packetPool.allocCopy(mp);
        meshtastic_TAK_Packet uncompressed = meshtastic_TAK_Packet_init_default;
        uncompressed.which_callsign_variant = meshtastic_TAK_Packet_callsign_uncompressed_tag;
        uncompressed.which_payload_variant = meshtastic_TAK_Packet_tak_pli_tag;
        uncompressed.payload_variant.tak_pli = t->payload_variant.tak_pli;
        auto length = unishox2_decompress_simple(t->callsign_variant.callsign_compressed, decompressedCopy->decoded.payload.size,
                                                 uncompressed.callsign_variant.callsign_uncompressed);
        decompressedCopy->decoded.payload.size =
            pb_encode_to_bytes(decompressedCopy->decoded.payload.bytes, sizeof(decompressedCopy->decoded.payload),
                               meshtastic_TAK_Packet_fields, &uncompressed);

        LOG_DEBUG("Compressed callsign: %d bytes\n", strlen(t->callsign_variant.callsign_compressed));
        LOG_DEBUG("Decompressed callsign: '%s' @ %d bytes\n", uncompressed.callsign_variant.callsign_uncompressed, length);
        service.sendToPhone(decompressedCopy);
    }
    return;
}