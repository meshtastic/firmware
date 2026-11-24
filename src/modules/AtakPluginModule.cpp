#include "AtakPluginModule.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "mesh-pb-constants.h"
#include <algorithm>

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule() : SinglePortModule("atak", meshtastic_PortNum_ATAK_PLUGIN_V2) {}

ProcessMessage AtakPluginModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp; // Passthrough - no processing needed, apps handle compression/decompression
    return ProcessMessage::CONTINUE;
}

bool AtakPluginModule::sendsTextAsGeoChat()
{
    return config.device.role == meshtastic_Config_DeviceConfig_Role_TAK ||
           config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
}

void AtakPluginModule::fillSender(meshtastic_TAKPacketV2 &takPacket)
{
    // Use TAK config for team/role if configured, otherwise use defaults (Cyan/TeamMember)
    if (moduleConfig.has_tak && moduleConfig.tak.team != meshtastic_Team_Unspecifed_Color) {
        takPacket.team = moduleConfig.tak.team;
    } else {
        takPacket.team = meshtastic_Team_Cyan;
    }

    if (moduleConfig.has_tak && moduleConfig.tak.role != meshtastic_MemberRole_Unspecifed) {
        takPacket.role = moduleConfig.tak.role;
    } else {
        takPacket.role = meshtastic_MemberRole_TeamMember;
    }

    strncpy(takPacket.callsign, owner.long_name, sizeof(takPacket.callsign) - 1);
    takPacket.callsign[sizeof(takPacket.callsign) - 1] = '\0';
}

bool AtakPluginModule::setPayload(meshtastic_MeshPacket *p, const meshtastic_TAKPacketV2 &takPacket)
{
    // Encode aside so a failure leaves p untouched
    uint8_t protobuf_bytes[sizeof(p->decoded.payload.bytes) - 1];
    size_t proto_size = pb_encode_to_bytes(protobuf_bytes, sizeof(protobuf_bytes), &meshtastic_TAKPacketV2_msg, &takPacket);
    if (proto_size == 0)
        return false;

    // Wire format: [flags byte][protobuf bytes]
    // Flags byte 0xFF is a reserved sentinel meaning the remainder is an
    // uncompressed raw protobuf payload (that is, no zstd dictionary ID is
    // encoded in the flags byte). Decoders must check for this sentinel before
    // applying any dictionary-ID masking/interpretation.
    p->decoded.portnum = meshtastic_PortNum_ATAK_PLUGIN_V2;
    p->decoded.payload.bytes[0] = 0xFF;
    memcpy(p->decoded.payload.bytes + 1, protobuf_bytes, proto_size);
    p->decoded.payload.size = proto_size + 1;
    return true;
}

// GeoChat envelope as the Meshtastic apps build it from an ATAK b-t-f event: uid
// GeoChat.<sender>.<chatroom>.<message id>, and chat.to unset for All Chat Rooms. Our sender uid is
// the !nodenum our PLI carries.
static void fillGeoChat(meshtastic_TAKPacketV2 &takPacket, NodeNum dest, PacketId id)
{
    takPacket.cot_type_id = meshtastic_CotType_CotType_b_t_f;
    takPacket.how = meshtastic_CotHow_CotHow_h_g_i_g_o;
    AtakPluginModule::fillSender(takPacket);

    char sender[10];
    snprintf(sender, sizeof(sender), "!%08x", nodeDB->getNodeNum());

    takPacket.which_payload_variant = meshtastic_TAKPacketV2_chat_tag;
    meshtastic_GeoChat &chat = takPacket.payload_variant.chat;
    const char *chatroom = "All Chat Rooms";
    if (!isBroadcast(dest)) {
        snprintf(chat.to, sizeof(chat.to), "!%08x", dest);
        chat.has_to = true;
        chatroom = chat.to;
    }

    snprintf(takPacket.uid, sizeof(takPacket.uid), "GeoChat.%s.%s.%08x", sender, chatroom, id);
}

// Room for the Data message on air: the frame, less the radio header and whichever of PKC, channel
// AEAD or an XEdDSA signature (64 bytes, tag and length) the router's perhapsEncode() will add
static size_t geoChatDataMax(NodeNum dest, ChannelIndex channel, bool pki)
{
    size_t overhead = MESHTASTIC_HEADER_LENGTH;
    if (pki) {
        overhead += MESHTASTIC_PKC_OVERHEAD;
    } else if (channels.isAEADEnabled(channel)) {
        overhead += MESHTASTIC_AEAD_OVERHEAD;
    }
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
    else if (config.security.packet_signature_policy !=
                 meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE &&
             (owner.is_licensed || isBroadcast(dest))) {
        overhead += XEDDSA_SIGNATURE_SIZE + 2;
    }
#endif
    return MAX_LORA_PAYLOAD_LEN - overhead;
}

// Message bytes that fit beside an envelope from fillGeoChat
static size_t geoChatRoom(const meshtastic_TAKPacketV2 &takPacket, size_t dataMax)
{
    size_t envelope;
    if (!pb_get_encoded_size(&envelope, &meshtastic_TAKPacketV2_msg, &takPacket))
        return 0;
    // The Data wrapper (portnum 2, payload tag and length 3, dest 5, bitfield 2), the flags byte, then
    // the text's tag and up to 2 length bytes, plus a second byte for the chat's own length past 127
    const size_t overhead = 12 + envelope + 4 + 1;
    if (overhead >= dataMax)
        return 0;
    return std::min(dataMax - overhead, sizeof(takPacket.payload_variant.chat.message) - 1);
}

// A TAKPacketV2 is ~2 KB, too much for the loop stack these run on (the composer's counter calls in
// from the screen draw). calloc() returns nullptr instead of aborting, and all-zero is init_zero.
static meshtastic_TAKPacketV2 *allocTakPacket()
{
    return static_cast<meshtastic_TAKPacketV2 *>(calloc(1, sizeof(meshtastic_TAKPacketV2)));
}

size_t AtakPluginModule::maxGeoChatBytes(NodeNum dest, ChannelIndex channel, bool pki)
{
    // The composer asks on every redraw: rebuild the envelope only when something it depends on changes
    struct Inputs {
        NodeNum dest;
        size_t dataMax;
        bool hasTak;
        meshtastic_Team team;
        meshtastic_MemberRole role;
        char callsign[sizeof(owner.long_name)];
    };
    static Inputs cachedInputs;
    static size_t cachedRoom = 0;
    static bool cacheValid = false;

    Inputs inputs;
    memset(&inputs, 0, sizeof(inputs)); // zeroed padding, so memcmp compares only the fields
    inputs.dest = dest;
    inputs.dataMax = geoChatDataMax(dest, channel, pki);
    inputs.hasTak = moduleConfig.has_tak;
    inputs.team = moduleConfig.tak.team;
    inputs.role = moduleConfig.tak.role;
    strncpy(inputs.callsign, owner.long_name, sizeof(inputs.callsign) - 1);
    if (cacheValid && memcmp(&inputs, &cachedInputs, sizeof(inputs)) == 0)
        return cachedRoom;

    meshtastic_TAKPacketV2 *takPacket = allocTakPacket();
    if (!takPacket)
        return 0;
    fillGeoChat(*takPacket, dest, 0);
    cachedRoom = geoChatRoom(*takPacket, inputs.dataMax);
    free(takPacket);
    cachedInputs = inputs;
    cacheValid = true;
    return cachedRoom;
}

bool AtakPluginModule::setGeoChatPayload(meshtastic_MeshPacket *p, const char *message, size_t len, size_t *carried)
{
    meshtastic_TAKPacketV2 *takPacket = allocTakPacket();
    if (!takPacket) {
        LOG_ERROR("No memory for GeoChat");
        return false;
    }
    fillGeoChat(*takPacket, p->to, p->id);

    const size_t room = geoChatRoom(*takPacket, geoChatDataMax(p->to, p->channel, p->pki_encrypted));
    if (len > room) {
        LOG_WARN("GeoChat text cut from %u to %u bytes", (unsigned)len, (unsigned)room);
        len = room;
        // Don't split a UTF-8 sequence
        while (len > 0 && (message[len] & 0xC0) == 0x80)
            len--;
    }
    meshtastic_GeoChat &chat = takPacket->payload_variant.chat;
    memcpy(chat.message, message, len);
    chat.message[len] = '\0';

    const bool ok = setPayload(p, *takPacket);
    free(takPacket);
    if (!ok) {
        LOG_ERROR("GeoChat encode failed");
        return false;
    }
    LOG_INFO("Send GeoChat id=0x%08x to=0x%08x, %u bytes of text", p->id, p->to, (unsigned)len);
    if (carried)
        *carried = len;
    return true;
}
