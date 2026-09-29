#include "PluginEcho.h"
#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "mesh/ModuleRegistry.h"
#include <cstring>

PluginEcho::PluginEcho() : SinglePortModule("PluginEcho", meshtastic_PortNum_PRIVATE_APP) {}

void PluginEcho::setup()
{
    LOG_INFO("PluginEcho: plugin loaded and initialized (port %u)", (unsigned)ourPortNum);
}

ProcessMessage PluginEcho::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return ProcessMessage::CONTINUE;
    }

    const NodeNum us = nodeDB->getNodeNum();
    if (mp.to != us || mp.decoded.payload.size == 0) {
        return ProcessMessage::CONTINUE;
    }

    meshtastic_MeshPacket *reply = allocDataPacket();
    if (!reply) {
        return ProcessMessage::CONTINUE;
    }

    size_t len = mp.decoded.payload.size;
    if (len > sizeof(reply->decoded.payload.bytes)) {
        len = sizeof(reply->decoded.payload.bytes);
    }
    memcpy(reply->decoded.payload.bytes, mp.decoded.payload.bytes, len);
    reply->decoded.payload.size = len;
    reply->to = mp.from;
    reply->channel = mp.channel;

    LOG_INFO("PluginEcho: echo %u bytes to 0x%x (from 0x%x id=0x%x)", (unsigned)len, reply->to, mp.from, mp.id);

    myReply = reply;
    return ProcessMessage::CONTINUE;
}

MESHTASTIC_REGISTER_MODULE(PluginEcho);
