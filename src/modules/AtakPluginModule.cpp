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

meshtastic_TAKPacket AtakPluginModule::cloneTAKPacketData(meshtastic_TAKPacket *t)
{
    meshtastic_TAKPacket clone = meshtastic_TAKPacket_init_zero;
    if (t->has_group) {
        clone.has_group = true;
        clone.group = t->group;
    }
    if (t->has_status) {
        clone.has_status = true;
        clone.status = t->status;
    }
    if (t->has_contact) {
        clone.has_contact = true;
        clone.contact = {0};
    }

    if (t->which_payload_variant == meshtastic_TAKPacket_pli_tag) {
        clone.which_payload_variant = meshtastic_TAKPacket_pli_tag;
        clone.payload_variant.pli = t->payload_variant.pli;
    } else if (t->which_payload_variant == meshtastic_TAKPacket_chat_tag) {
        clone.which_payload_variant = meshtastic_TAKPacket_chat_tag;
        clone.payload_variant.chat = {0};
    }

    return clone;
}

void AtakPluginModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAKPacket *t)
{
    // From Phone (EUD)
    if (mp.from == 0) {
        LOG_DEBUG("Received uncompressed TAK payload from phone with %d bytes\n", mp.decoded.payload.size);
        // Compress for LoRA transport
        auto compressed = cloneTAKPacketData(t);
        compressed.is_compressed = true;
        if (t->has_contact) {
            auto length = unishox2_compress_simple(t->contact.callsign, strlen(t->contact.callsign), compressed.contact.callsign);
            LOG_DEBUG("Uncompressed callsign '%s' - %d bytes\n", t->contact.callsign, strlen(t->contact.callsign));
            LOG_DEBUG("Compressed callsign '%s' - %d bytes\n", t->contact.callsign, length);

            length = unishox2_compress_simple(t->contact.device_callsign, strlen(t->contact.device_callsign),
                                              compressed.contact.device_callsign);
            LOG_DEBUG("Uncompressed device_callsign '%s' - %d bytes\n", t->contact.device_callsign,
                      strlen(t->contact.device_callsign));
            LOG_DEBUG("Compressed device_callsign '%s' - %d bytes\n", compressed.contact.device_callsign, length);
        }
        if (t->which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_compress_simple(t->payload_variant.chat.message, strlen(t->payload_variant.chat.message),
                                                   compressed.payload_variant.chat.message);
            LOG_DEBUG("Uncompressed chat message '%s' - %d bytes\n", t->payload_variant.chat.message,
                      strlen(t->payload_variant.chat.message));
            LOG_DEBUG("Compressed chat message '%s' - %d bytes\n", compressed.payload_variant.chat.message, length);

            if (t->payload_variant.chat.has_to) {
                compressed.payload_variant.chat.has_to = true;
                length = unishox2_compress_simple(t->payload_variant.chat.to, strlen(t->payload_variant.chat.to),
                                                  compressed.payload_variant.chat.to);
                LOG_DEBUG("Uncompressed chat to '%s' - %d bytes\n", t->payload_variant.chat.to,
                          strlen(t->payload_variant.chat.to));
                LOG_DEBUG("Compressed chat to '%s' - %d bytes\n", compressed.payload_variant.chat.to, length);
            }
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
        auto uncompressed = cloneTAKPacketData(t);
        uncompressed.is_compressed = false;
        if (t->has_contact) {
            auto length =
                unishox2_decompress_simple(t->contact.callsign, strlen(t->contact.callsign), uncompressed.contact.callsign);

            LOG_DEBUG("Compressed callsign: %d bytes\n", strlen(t->contact.callsign));
            LOG_DEBUG("Decompressed callsign: '%s' @ %d bytes\n", uncompressed.contact.callsign, length);

            length = unishox2_decompress_simple(t->contact.device_callsign, strlen(t->contact.device_callsign),
                                                uncompressed.contact.device_callsign);

            LOG_DEBUG("Compressed device_callsign: %d bytes\n", strlen(t->contact.device_callsign));
            LOG_DEBUG("Decompressed device_callsign: '%s' @ %d bytes\n", uncompressed.contact.device_callsign, length);
        }
        if (uncompressed.which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_decompress_simple(t->payload_variant.chat.message, strlen(t->payload_variant.chat.message),
                                                     uncompressed.payload_variant.chat.message);
            LOG_DEBUG("Compressed chat message: %d bytes\n", strlen(t->payload_variant.chat.message));
            LOG_DEBUG("Decompressed chat message: '%s' @ %d bytes\n", uncompressed.payload_variant.chat.message, length);

            if (t->payload_variant.chat.has_to) {
                uncompressed.payload_variant.chat.has_to = true;
                length = unishox2_decompress_simple(t->payload_variant.chat.to, strlen(t->payload_variant.chat.to),
                                                    uncompressed.payload_variant.chat.to);
                LOG_DEBUG("Compressed chat to: %d bytes\n", strlen(t->payload_variant.chat.to));
                LOG_DEBUG("Decompressed chat to: '%s' @ %d bytes\n", uncompressed.payload_variant.chat.to, length);
            }
        }
        decompressedCopy->decoded.payload.size =
            pb_encode_to_bytes(decompressedCopy->decoded.payload.bytes, sizeof(decompressedCopy->decoded.payload),
                               meshtastic_TAKPacket_fields, &uncompressed);

        service.sendToPhone(decompressedCopy);
    }
    return;
}