#include "PluginEcho.h"
#include "DebugConfiguration.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "mesh/MeshTypes.h"
#include "mesh/ModuleRegistry.h"
#include <cstring>

static const char kPing[] = "/ping";
static const char kPong[] = "pong";

PluginEcho::PluginEcho() : SinglePortModule("PluginEcho", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
    isPromiscuous = true; // channel broadcasts aren't addressed to us
}

void PluginEcho::setup()
{
    LOG_INFO("PluginEcho: plugin loaded and initialized (port %u)", (unsigned)ourPortNum);
}

ProcessMessage PluginEcho::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag || isFromUs(&mp)) {
        return ProcessMessage::CONTINUE;
    }

    const bool dm = isToUs(&mp);
    if (!dm && !isBroadcast(mp.to)) {
        return ProcessMessage::CONTINUE;
    }

    const char *text = reinterpret_cast<const char *>(mp.decoded.payload.bytes);
    const size_t len = mp.decoded.payload.size;
    const size_t cmdLen = sizeof(kPing) - 1;
    if (len < cmdLen || memcmp(text, kPing, cmdLen) != 0 || (len > cmdLen && text[cmdLen] != ' ')) {
        return ProcessMessage::CONTINUE;
    }

    meshtastic_MeshPacket *reply = allocDataPacket();
    if (!reply) {
        return ProcessMessage::CONTINUE;
    }

    // "pong" + everything after "/ping", so "/ping abc" -> "pong abc"
    const size_t cap = sizeof(reply->decoded.payload.bytes);
    const size_t pongLen = sizeof(kPong) - 1;
    size_t rest = len - cmdLen;
    if (rest > cap - pongLen) {
        rest = cap - pongLen;
    }
    memcpy(reply->decoded.payload.bytes, kPong, pongLen);
    memcpy(reply->decoded.payload.bytes + pongLen, text + cmdLen, rest);
    reply->decoded.payload.size = pongLen + rest;
    reply->decoded.reply_id = mp.id;
    reply->to = dm ? mp.from : NODENUM_BROADCAST;
    reply->channel = mp.channel;

    LOG_INFO("PluginEcho: pong %u bytes to 0x%x (from 0x%x id=0x%x)", (unsigned)reply->decoded.payload.size, reply->to, mp.from,
             mp.id);

    service->sendToMesh(reply);
    return ProcessMessage::CONTINUE;
}

MESHTASTIC_REGISTER_MODULE(PluginEcho);
