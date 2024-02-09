#include "AtakPluginModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "meshtastic/atak.pb.h"

extern "C" {
#include "mesh/compression/unishox2.h"
}

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule()
    : ProtobufModule("atak", meshtastic_PortNum_ATAK_PLUGIN, &meshtastic_TAKPacket_msg), concurrency::OSThread("AtakPluginModule")
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
    // From Phone (EUD)
    if (mp.from == 0) {
        LOG_DEBUG("Received uncompressed TAK payload from phone with %d bytes\n", mp.decoded.payload.size);
        // Compress for LoRA transport
        meshtastic_TAKPacket compressed = *t;
        compressed.is_compressed = true;
        if (t->has_contact) {
            auto length = unishox2_compress_simple(t->contact.callsign, sizeof(t->contact.callsign), compressed.contact.callsign);
            LOG_DEBUG("Uncompressed callsign %d bytes\n", strlen(t->contact.callsign));
            LOG_DEBUG("Compressed callsign %d bytes\n", length);

            length = unishox2_compress_simple(t->contact.device_callsign, sizeof(t->contact.device_callsign),
                                              compressed.contact.device_callsign);
            LOG_DEBUG("Uncompressed device_callsign %d bytes\n", strlen(t->contact.device_callsign));
            LOG_DEBUG("Compressed device_callsign %d bytes\n", length);
        }

        if (compressed.which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_compress_simple(t->payload_variant.chat.message, sizeof(t->payload_variant.chat.message),
                                                   compressed.payload_variant.chat.message);
            LOG_DEBUG("Uncompressed chat message %d bytes\n", strlen(compressed.payload_variant.chat.message));
            LOG_DEBUG("Compressed chat message %d bytes\n", length);
        }
        mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                     meshtastic_TAKPacket_fields, &compressed);
        LOG_DEBUG("Final payload size of %d bytes\n", mp.decoded.payload.size);
    } else {
        if (!t->is_compressed) {
            // Not compressed. Something is wrong
            LOG_ERROR("Received uncompressed TAKPacket over radio!\n");
            return;
        }

        // Decompress for Phone (EUD)
        auto decompressedCopy = packetPool.allocCopy(mp);
        meshtastic_TAKPacket uncompressed = *t;
        if (t->has_contact) {
            auto length =
                unishox2_decompress_simple(t->contact.callsign, sizeof(t->contact.callsign), uncompressed.contact.callsign);

            LOG_DEBUG("Compressed callsign: %d bytes\n", strlen(t->contact.callsign));
            LOG_DEBUG("Decompressed callsign: '%s' @ %d bytes\n", uncompressed.contact.callsign, length);

            length = unishox2_decompress_simple(t->contact.device_callsign, sizeof(t->contact.device_callsign),
                                                uncompressed.contact.device_callsign);

            LOG_DEBUG("Compressed device_callsign: %d bytes\n", strlen(t->contact.device_callsign));
            LOG_DEBUG("Decompressed device_callsign: '%s' @ %d bytes\n", uncompressed.contact.device_callsign, length);
        }
        if (uncompressed.which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_decompress_simple(t->payload_variant.chat.message, sizeof(t->payload_variant.chat.message),
                                                     uncompressed.payload_variant.chat.message);
            LOG_DEBUG("Compressed chat message: %d bytes\n", strlen(t->payload_variant.chat.message));
            LOG_DEBUG("Decompressed chat message: '%s' @ %d bytes\n", uncompressed.payload_variant.chat.message, length);
        }
        decompressedCopy->decoded.payload.size =
            pb_encode_to_bytes(decompressedCopy->decoded.payload.bytes, sizeof(decompressedCopy->decoded.payload),
                               meshtastic_TAKPacket_fields, &uncompressed);

        service.sendToPhone(decompressedCopy);
    }
    return;
}