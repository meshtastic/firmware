#include "configuration.h"
#include "ReplyPlugin.h"
#include "MeshService.h"
#include "main.h"

#include <assert.h>

// Create an a static instance of our plugin - this registers with the plugin system
ReplyPlugin replyPlugin;

bool ReplyPlugin::handleReceived(const MeshPacket &req)
{
    auto &p = req.decoded.data;
    // The incoming message is in p.payload
    DEBUG_MSG("Received message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);

    screen->print("Sending reply\n");

    const char *replyStr = "Message Received";
    auto reply = allocDataPacket(); // Allocate a packet for sending
    reply->decoded.data.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.data.payload.bytes, replyStr, reply->decoded.data.payload.size);
    setReplyTo(reply, req); // Set packet params so that this packet is marked as a reply to a previous request
    service.sendToMesh(reply); // Queue the reply for sending

    return true; // We handled it
}
