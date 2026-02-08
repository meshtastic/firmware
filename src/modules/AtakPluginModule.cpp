#include "AtakPluginModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "mesh/compression/unishox2.h"
#include "meshtastic/atak.pb.h"

AtakPluginModule *atakPluginModule;

/**
 * Get actual string length for nanopb char array fields.
 * Nanopb stores strings as fixed-size char arrays that may contain embedded nulls.
 * strlen() would stop at the first null, but we need to find the last non-null character.
 * This is critical for Android UIDs that can contain 0x00 bytes (e.g., ANDROID-e7e455b40002429d).
 */
static size_t pb_string_length(const char *str, size_t max_len)
{
    size_t len = 0;
    for (size_t i = 0; i < max_len; i++) {
        if (str[i] != '\0') {
            len = i + 1;
        }
    }
    return len;
}

AtakPluginModule::AtakPluginModule()
    : ProtobufModule("atak", meshtastic_PortNum_ATAK_PLUGIN, &meshtastic_TAKPacket_msg), concurrency::OSThread("AtakPlugin")
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
    } else if (t->which_payload_variant == meshtastic_TAKPacket_detail_tag) {
        clone.which_payload_variant = meshtastic_TAKPacket_detail_tag;
        clone.payload_variant.detail.size = t->payload_variant.detail.size;
        memcpy(clone.payload_variant.detail.bytes, t->payload_variant.detail.bytes, t->payload_variant.detail.size);
    }

    return clone;
}

void AtakPluginModule::alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAKPacket *t)
{
    // From Phone (EUD)
    if (mp.from == 0) {
        LOG_DEBUG("Received uncompressed TAK payload from phone: %d bytes", mp.decoded.payload.size);
        // Compress for LoRA transport
        auto compressed = cloneTAKPacketData(t);
        compressed.is_compressed = true;
        if (t->has_contact) {
            auto length = unishox2_compress_lines(
                t->contact.callsign, pb_string_length(t->contact.callsign, sizeof(t->contact.callsign)),
                compressed.contact.callsign, sizeof(compressed.contact.callsign) - 1, USX_PSET_DFLT, NULL);
            if (length < 0) {
                LOG_WARN("Compress overflow contact.callsign. Revert to uncompressed packet");
                return;
            }
            LOG_DEBUG("Compressed callsign: %d bytes", length);
            length = unishox2_compress_lines(
                t->contact.device_callsign, pb_string_length(t->contact.device_callsign, sizeof(t->contact.device_callsign)),
                compressed.contact.device_callsign, sizeof(compressed.contact.device_callsign) - 1, USX_PSET_DFLT, NULL);
            if (length < 0) {
                LOG_WARN("Compress overflow contact.device_callsign. Revert to uncompressed packet");
                return;
            }
            LOG_DEBUG("Compressed device_callsign: %d bytes", length);
        }
        if (t->which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_compress_lines(
                t->payload_variant.chat.message,
                pb_string_length(t->payload_variant.chat.message, sizeof(t->payload_variant.chat.message)),
                compressed.payload_variant.chat.message, sizeof(compressed.payload_variant.chat.message) - 1, USX_PSET_DFLT,
                NULL);
            if (length < 0) {
                LOG_WARN("Compress overflow chat.message. Revert to uncompressed packet");
                return;
            }
            LOG_DEBUG("Compressed chat message: %d bytes", length);

            if (t->payload_variant.chat.has_to) {
                compressed.payload_variant.chat.has_to = true;
                length = unishox2_compress_lines(
                    t->payload_variant.chat.to, pb_string_length(t->payload_variant.chat.to, sizeof(t->payload_variant.chat.to)),
                    compressed.payload_variant.chat.to, sizeof(compressed.payload_variant.chat.to) - 1, USX_PSET_DFLT, NULL);
                if (length < 0) {
                    LOG_WARN("Compress overflow chat.to. Revert to uncompressed packet");
                    return;
                }
                LOG_DEBUG("Compressed chat to: %d bytes", length);
            }

            if (t->payload_variant.chat.has_to_callsign) {
                compressed.payload_variant.chat.has_to_callsign = true;
                length = unishox2_compress_lines(
                    t->payload_variant.chat.to_callsign,
                    pb_string_length(t->payload_variant.chat.to_callsign, sizeof(t->payload_variant.chat.to_callsign)),
                    compressed.payload_variant.chat.to_callsign, sizeof(compressed.payload_variant.chat.to_callsign) - 1,
                    USX_PSET_DFLT, NULL);
                if (length < 0) {
                    LOG_WARN("Compress overflow chat.to_callsign. Revert to uncompressed packet");
                    return;
                }
                LOG_DEBUG("Compressed chat to_callsign: %d bytes", length);
            }
        }
        mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                     meshtastic_TAKPacket_fields, &compressed);
        LOG_DEBUG("Final payload: %d bytes", mp.decoded.payload.size);
    } else {
        if (!t->is_compressed) {
            // Not compressed. Something is wrong
            LOG_WARN("Received uncompressed TAKPacket over radio! Skip");
            return;
        }

        // Decompress for Phone (EUD)
        auto uncompressed = cloneTAKPacketData(t);
        uncompressed.is_compressed = false;
        if (t->has_contact) {
            auto length = unishox2_decompress_lines(
                t->contact.callsign, pb_string_length(t->contact.callsign, sizeof(t->contact.callsign)),
                uncompressed.contact.callsign, sizeof(uncompressed.contact.callsign) - 1, USX_PSET_DFLT, NULL);
            if (length < 0) {
                LOG_WARN("Decompress overflow contact.callsign. Bailing out");
                return;
            }
            LOG_DEBUG("Decompressed callsign: %d bytes", length);

            length = unishox2_decompress_lines(
                t->contact.device_callsign, pb_string_length(t->contact.device_callsign, sizeof(t->contact.device_callsign)),
                uncompressed.contact.device_callsign, sizeof(uncompressed.contact.device_callsign) - 1, USX_PSET_DFLT, NULL);
            if (length < 0) {
                LOG_WARN("Decompress overflow contact.device_callsign. Bailing out");
                return;
            }
            LOG_DEBUG("Decompressed device_callsign: %d bytes", length);
        }
        if (uncompressed.which_payload_variant == meshtastic_TAKPacket_chat_tag) {
            auto length = unishox2_decompress_lines(
                t->payload_variant.chat.message,
                pb_string_length(t->payload_variant.chat.message, sizeof(t->payload_variant.chat.message)),
                uncompressed.payload_variant.chat.message, sizeof(uncompressed.payload_variant.chat.message) - 1, USX_PSET_DFLT,
                NULL);
            if (length < 0) {
                LOG_WARN("Decompress overflow chat.message. Bailing out");
                return;
            }
            LOG_DEBUG("Decompressed chat message: %d bytes", length);

            if (t->payload_variant.chat.has_to) {
                uncompressed.payload_variant.chat.has_to = true;
                length = unishox2_decompress_lines(
                    t->payload_variant.chat.to, pb_string_length(t->payload_variant.chat.to, sizeof(t->payload_variant.chat.to)),
                    uncompressed.payload_variant.chat.to, sizeof(uncompressed.payload_variant.chat.to) - 1, USX_PSET_DFLT, NULL);
                if (length < 0) {
                    LOG_WARN("Decompress overflow chat.to. Bailing out");
                    return;
                }
                LOG_DEBUG("Decompressed chat to: %d bytes", length);
            }

            if (t->payload_variant.chat.has_to_callsign) {
                uncompressed.payload_variant.chat.has_to_callsign = true;
                length = unishox2_decompress_lines(
                    t->payload_variant.chat.to_callsign,
                    pb_string_length(t->payload_variant.chat.to_callsign, sizeof(t->payload_variant.chat.to_callsign)),
                    uncompressed.payload_variant.chat.to_callsign, sizeof(uncompressed.payload_variant.chat.to_callsign) - 1,
                    USX_PSET_DFLT, NULL);
                if (length < 0) {
                    LOG_WARN("Decompress overflow chat.to_callsign. Bailing out");
                    return;
                }
                LOG_DEBUG("Decompressed chat to_callsign: %d bytes", length);
            }
        }
        auto decompressedCopy = packetPool.allocCopy(mp);
        decompressedCopy->decoded.payload.size =
            pb_encode_to_bytes(decompressedCopy->decoded.payload.bytes, sizeof(decompressedCopy->decoded.payload),
                               meshtastic_TAKPacket_fields, &uncompressed);

        service->sendToPhone(decompressedCopy);
    }
    return;
}