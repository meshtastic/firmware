#include "AtakPluginModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "meshtastic/atak.pb.h"

extern "C"
{
#include "mesh/compression/unishox2.h"
}

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule()
    : ProtobufModule("atak", meshtastic_PortNum_ATAK_PLUGIN, &meshtastic_TAKPacket_msg),
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

bool AtakPluginModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_TAKPacket *r)
{
    return false;
}

void AtakPluginModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAKPacket *t)
{
    // Not PLI, ignore it
    if (t->which_payload_variant != meshtastic_TAKPacket_pli_tag)
        return;

    // From Phone (EUD)
    if (mp.from == 0)
    {
        // LOG_DEBUG("Received uncompressed TAK payload from phone with %d bytes\n", mp.decoded.payload.size);
        // // Compress for LoRA transport
        // meshtastic_TAKPacket compressed = meshtastic_TAKPacket_init_default;
        // memccpy(&compressed, t, sizeof(meshtastic_TAKPacket), sizeof(meshtastic_TAKPacket));
        // compressed.contact.which_callsign_variant = meshtastic_Contact_callsign_compressed_tag;
        // auto length =
        //     unishox2_compress_simple(t->contact.callsign_variant.callsign_uncompressed, sizeof(t->contact.callsign_variant.callsign_uncompressed),
        //                              compressed.contact.callsign_variant.callsign_compressed);
        // memset(compressed.contact.callsign_variant.callsign_uncompressed, '\0', strlen(compressed.contact.callsign_variant.callsign_uncompressed));
        // LOG_DEBUG("Uncompressed callsign %d bytes\n", strlen(t->contact.callsign_variant.callsign_uncompressed));
        // LOG_DEBUG("Compressed callsign %d bytes\n", length);
        // mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
        //                                              meshtastic_TAKPacket_fields, &compressed);
        // LOG_DEBUG("Final payload size of %d bytes\n", mp.decoded.payload.size);
    }
    else
    {
        // if (!t->has_contact || t->contact.which_callsign_variant != meshtastic_Contact_callsign_compressed_tag)
        // {
        //     // Not compressed. Something
        //     LOG_ERROR("Received uncompressed TAKPacket atak plugin msg!\n");
        //     return;
        // }
        // // From another node on the mesh
        // // Decompress for Phone (EUD)
        // auto decompressedCopy = packetPool.allocCopy(mp);
        // meshtastic_TAKPacket uncompressed = meshtastic_TAKPacket_init_default;
        // memccpy(&uncompressed, t, sizeof(meshtastic_TAKPacket), sizeof(meshtastic_TAKPacket));
        // auto length = unishox2_decompress_simple(t->contact.callsign_variant.callsign_compressed, decompressedCopy->decoded.payload.size,
        //                                          uncompressed.contact.callsign_variant.callsign_uncompressed);
        // decompressedCopy->decoded.payload.size =
        //     pb_encode_to_bytes(decompressedCopy->decoded.payload.bytes, sizeof(decompressedCopy->decoded.payload),
        //                        meshtastic_TAKPacket_fields, &uncompressed);

        // LOG_DEBUG("Compressed callsign: %d bytes\n", strlen(t->contact.callsign_variant.callsign_compressed));
        // LOG_DEBUG("Decompressed callsign: '%s' @ %d bytes\n", uncompressed.contact.callsign_variant.callsign_uncompressed, length);
        // service.sendToPhone(decompressedCopy);
    }
    return;
}