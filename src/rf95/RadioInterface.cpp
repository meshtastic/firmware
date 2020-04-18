#include "CustomRF95.h"
#include "NodeDB.h"
#include "assert.h"
#include "configuration.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

RadioInterface::RadioInterface() {}

ErrorCode SimRadio::send(MeshPacket *p)
{
    DEBUG_MSG("SimRadio.send\n");
    packetPool.release(p);
    return ERRNO_OK;
}

void RadioInterface::deliverToReceiver(MeshPacket *p)
{
    assert(rxDest);
    assert(rxDest->enqueue(p, 0)); // NOWAIT - fixme, if queue is full, delete older messages
}