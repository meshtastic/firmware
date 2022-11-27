#include "BlackLagerModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"

#include <assert.h>

BlackLagerModule *blackLagerModule;

/**
 * Text messaging module with digital signatures.
 */
ProcessMessage BlackLagerModule::handleReceived(const MeshPacket &mp)
{
    assert(currentRequest); // should always be !NULL
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    DEBUG_MSG("Received Black Lager message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);

    screen->print("Sending Black Lager reply\n");

    const char *replyStr = "Black Lager message Received";
    auto reply = allocDataPacket();                 // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
