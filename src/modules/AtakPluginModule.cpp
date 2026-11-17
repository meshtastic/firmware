#include "AtakPluginModule.h"
#include "MeshTypes.h"
#include "NodeDB.h"
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
// GeoChat.<sender>.<chatroom>.<message id>, device_callsign <sender>|<message id>, and chat.to
// unset for All Chat Rooms. Our sender uid is the !nodenum our PLI carries.
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
    snprintf(takPacket.device_callsign, sizeof(takPacket.device_callsign), "%s|%08x", sender, id);
}

// Message bytes that fit beside an envelope from fillGeoChat
static size_t geoChatRoom(const meshtastic_TAKPacketV2 &takPacket)
{
    size_t envelope;
    if (!pb_get_encoded_size(&envelope, &meshtastic_TAKPacketV2_msg, &takPacket))
        return 0;
    // Flags byte, then the text's tag and up to 2 length bytes, plus a second byte for the chat's
    // own length once it passes 127
    const size_t overhead = envelope + 4 + 1;
    if (overhead >= meshtastic_Constants_DATA_PAYLOAD_LEN)
        return 0;
    return std::min(meshtastic_Constants_DATA_PAYLOAD_LEN - overhead, sizeof(takPacket.payload_variant.chat.message) - 1);
}

// A TAKPacketV2 is ~2 KB, too much for the loop stack these run on (the composer's counter calls in
// from the screen draw). calloc() returns nullptr instead of aborting, and all-zero is init_zero.
static meshtastic_TAKPacketV2 *allocTakPacket()
{
    return static_cast<meshtastic_TAKPacketV2 *>(calloc(1, sizeof(meshtastic_TAKPacketV2)));
}

size_t AtakPluginModule::maxGeoChatBytes(NodeNum dest)
{
    meshtastic_TAKPacketV2 *takPacket = allocTakPacket();
    if (!takPacket)
        return 0;
    fillGeoChat(*takPacket, dest, 0);
    const size_t room = geoChatRoom(*takPacket);
    free(takPacket);
    return room;
}

bool AtakPluginModule::setGeoChatPayload(meshtastic_MeshPacket *p, const char *message, size_t len)
{
    meshtastic_TAKPacketV2 *takPacket = allocTakPacket();
    if (!takPacket) {
        LOG_ERROR("No memory for GeoChat");
        return false;
    }
    fillGeoChat(*takPacket, p->to, p->id);

    const size_t room = geoChatRoom(*takPacket);
    if (len > room) {
        LOG_WARN("GeoChat text cut from %u to %u bytes", (unsigned)len, (unsigned)room);
        len = room;
        // Don't split a UTF-8 sequence
        while (len > 0 && (message[len] & 0xC0) == 0x80)
            len--;
    }
    // message may be p's own payload, so it's copied out before setPayload overwrites it
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
    return true;
}
