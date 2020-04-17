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

void RadioInterface::deliverToReceiverISR(MeshPacket *p, BaseType_t *higherPriWoken)
{
    assert(rxDest);
    assert(rxDest->enqueueFromISR(p, higherPriWoken)); // NOWAIT - fixme, if queue is full, delete older messages
}