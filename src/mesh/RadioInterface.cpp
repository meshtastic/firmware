
#include "RadioInterface.h"
#include "NodeDB.h"
#include "assert.h"
#include "configuration.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

// 1kb was too small
#define RADIO_STACK_SIZE 4096

RadioInterface::RadioInterface() : txQueue(MAX_TX_QUEUE)
{
    assert(sizeof(PacketHeader) == 4); // make sure the compiler did what we expected
}

bool RadioInterface::init()
{
    start("radio", RADIO_STACK_SIZE); // Start our worker thread

    return true;
}

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

/***
 * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of payload bytes to send
 */
size_t RadioInterface::beginSending(MeshPacket *p)
{
    assert(!sendingPacket);

    // DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(p->has_payload);

    lastTxStart = millis();

    PacketHeader *h = (PacketHeader *)radiobuf;

    h->from = p->from;
    h->to = p->to;
    h->flags = 0;
    h->id = p->id;

    // if the sender nodenum is zero, that means uninitialized
    assert(h->from);

    size_t numbytes = pb_encode_to_bytes(radiobuf + sizeof(PacketHeader), sizeof(radiobuf), SubPacket_fields, &p->payload) +
                      sizeof(PacketHeader);

    assert(numbytes <= MAX_RHPACKETLEN);

    sendingPacket = p;
    return numbytes;
}