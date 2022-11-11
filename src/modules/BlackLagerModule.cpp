#include "configuration.h"
#include "ReplyModule.h"
#include "MeshService.h"
#include "main.h"

#include <assert.h>

MeshPacket *ReplyModule::allocReply()
{
    assert(currentRequest); // should always be !NULL
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    DEBUG_MSG("Received message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);

    screen->print("Sending reply\n");

    const char *replyStr = "Message Received";
    auto reply = allocDataPacket();                      // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return reply;
}
