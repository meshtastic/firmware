#include "RadioLibInterface.h"
#include "MeshTypes.h"
#include "mesh-pb-constants.h"
#include <NodeDB.h> // FIXME, this class shouldn't need to look into nodedb
#include <configuration.h>
#include <pb_decode.h>
#include <pb_encode.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi, PhysicalLayer *_iface)
    : module(cs, irq, rst, busy, spi, spiSettings), iface(_iface)
{
    assert(!instance); // We assume only one for now
    instance = this;
}

void INTERRUPT_ATTR RadioLibInterface::isrRxLevel0()
{
    instance->pending = ISR_RX;
    instance->disableInterrupt();
}

void INTERRUPT_ATTR RadioLibInterface::isrTxLevel0()
{
    instance->pending = ISR_TX;
    instance->disableInterrupt();
}

/** Our ISR code currently needs this to find our active instance
 */
RadioLibInterface *RadioLibInterface::instance;

/**
 * Convert our modemConfig enum into wf, sf, etc...
 */
void RadioLibInterface::applyModemConfig()
{
    switch (modemConfig) {
    case Bw125Cr45Sf128: ///< Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Default medium range
        bw = 125;
        cr = 5;
        sf = 7;
        break;
    case Bw500Cr45Sf128: ///< Bw = 500 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Fast+short range
        bw = 500;
        cr = 5;
        sf = 7;
        break;
    case Bw31_25Cr48Sf512: ///< Bw = 31.25 kHz, Cr = 4/8, Sf = 512chips/symbol, CRC on. Slow+long range
        bw = 31.25;
        cr = 8;
        sf = 9;
        break;
    case Bw125Cr48Sf4096:
        bw = 125;
        cr = 8;
        sf = 12;
        break;
    default:
        assert(0); // Unknown enum
    }
}

/// Send a packet (possibly by enquing in a private fifo).  This routine will
/// later free() the packet to pool.  This routine is not allowed to stall because it is called from
/// bluetooth comms code.  If the txmit queue is empty it might return an error
ErrorCode RadioLibInterface::send(MeshPacket *p)
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    if (canSendImmediately()) {
        // if the radio is idle, we can send right away
        DEBUG_MSG("immediate send on mesh fr=0x%x,to=0x%x,id=%d\n (txGood=%d,rxGood=%d,rxBad=%d)\n", p->from, p->to, p->id,
                  txGood, rxGood, rxBad);

        startSend(p);
        return ERRNO_OK;
    } else {
        DEBUG_MSG("enqueuing packet for send from=0x%x, to=0x%x\n", p->from, p->to);
        ErrorCode res = txQueue.enqueue(p, 0) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (res != ERRNO_OK) // we weren't able to queue it, so we must drop it to prevent leaks
            packetPool.release(p);

        return res;
    }
}

bool RadioLibInterface::canSleep()
{
    bool res = txQueue.isEmpty();
    if (!res) // only print debug messages if we are vetoing sleep
        DEBUG_MSG("radio wait to sleep, txEmpty=%d\n", txQueue.isEmpty());

    return res;
}

void RadioLibInterface::loop()
{
    PendingISR wasPending = pending; // atomic read
    if (wasPending) {
        pending = ISR_NONE; // If the flag was set, it is _guaranteed_ the ISR won't be running, because it masked itself

        if (wasPending == ISR_TX)
            handleTransmitInterrupt();
        else if (wasPending == ISR_RX)
            handleReceiveInterrupt();
        else
            assert(0);

        startNextWork();
    }
}

void RadioLibInterface::startNextWork()
{
    // First send any outgoing packets we have ready
    MeshPacket *txp = txQueue.dequeuePtr(0);
    if (txp)
        startSend(txp);
    else {
        // Nothing to send, let's switch back to receive mode
        startReceive();
    }
}

void RadioLibInterface::handleTransmitInterrupt()
{
    DEBUG_MSG("handling lora TX interrupt\n");
    assert(sendingPacket); // Were we sending?

    completeSending();
}

void RadioLibInterface::completeSending()
{
    if (sendingPacket) {
        txGood++;

        // We are done sending that packet, release it
        packetPool.release(sendingPacket);
        sendingPacket = NULL;
        // DEBUG_MSG("Done with send\n");
    }
}

void RadioLibInterface::handleReceiveInterrupt()
{
    assert(isReceiving);
    isReceiving = false;

    DEBUG_MSG("handling lora RX interrupt\n");

    // read the number of actually received bytes
    size_t length = iface->getPacketLength();

    int state = iface->readData(radiobuf, length);
    if (state != ERR_NONE) {
        DEBUG_MSG("ignoring received packet due to error=%d\n", state);
        rxBad++;
    } else {
        // Skip the 4 headers that are at the beginning of the rxBuf
        int32_t payloadLen = length - sizeof(PacketHeader);
        const uint8_t *payload = radiobuf + sizeof(PacketHeader);

        // check for short packets
        if (payloadLen < 0) {
            DEBUG_MSG("ignoring received packet too short\n");
            rxBad++;
        } else {
            const PacketHeader *h = (PacketHeader *)radiobuf;
            uint8_t ourAddr = nodeDB.getNodeNum();

            if (h->to != 255 && h->to != ourAddr) {
                DEBUG_MSG("ignoring packet not sent to us\n");
            } else {
                MeshPacket *mp = packetPool.allocZeroed();

                SubPacket *p = &mp->payload;

                mp->from = h->from;
                mp->to = h->to;
                mp->id = h->id;
                addReceiveMetadata(mp);

                if (!pb_decode_from_bytes(payload, payloadLen, SubPacket_fields, p)) {
                    DEBUG_MSG("Invalid protobufs in received mesh packet, discarding.\n");
                    packetPool.release(mp);
                    // rxBad++; not really a hw error
                } else {
                    // parsing was successful, queue for our recipient
                    mp->has_payload = true;
                    rxGood++;

                    deliverToReceiver(mp);
                }
            }
        }
    }
}

/** start an immediate transmit */
void RadioLibInterface::startSend(MeshPacket *txp)
{
    size_t numbytes = beginSending(txp);

    int res = iface->startTransmit(radiobuf, numbytes);
    assert(res == ERR_NONE);

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrTxLevel0);
}
