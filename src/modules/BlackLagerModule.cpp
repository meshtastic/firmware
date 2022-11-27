#include "BlackLagerModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"

#include <assert.h>

BlackLagerModule *blackLagerModule;

/**
 * Text messaging module with digital signatures.
 */
ProcessMessage BlackLagerModule::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded;
    DEBUG_MSG("Received black lager msg from=0x%0x, id=0x%x, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);

    const char *replyStr = "Black Lager message Received";
    auto reply = allocDataPacket();                 // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
