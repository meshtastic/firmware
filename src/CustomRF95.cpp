#include "CustomRF95.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include "configuration.h"
#include "assert.h"
#include "NodeDB.h"

/// A temporary buffer used for sending/receving packets, sized to hold the biggest buffer we might need
#define MAX_RHPACKETLEN 251
static uint8_t radiobuf[MAX_RHPACKETLEN];

CustomRF95::CustomRF95(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest)
    : RH_RF95(NSS_GPIO, DIO0_GPIO),
      pool(_pool),
      rxDest(_rxDest),
      txQueue(MAX_TX_QUEUE),
      sendingPacket(NULL)
{
}

bool CustomRF95::canSleep()
{
    // We allow initializing mode, because sometimes while testing we don't ever call init() to turn on the hardware
    return (_mode == RHModeInitialising || _mode == RHModeIdle || _mode == RHModeRx) && !_isReceiving && txQueue.isEmpty(); 
}

bool CustomRF95::sleep()
{
    // we no longer care about interrupts from this device
    prepareDeepSleep();

    // FIXME - leave the device state in rx mode instead
    return RH_RF95::sleep();
}

bool CustomRF95::init()
{
    bool ok = RH_RF95::init();

    return ok;
}

/// Send a packet (possibly by enquing in a private fifo).  This routine will
/// later free() the packet to pool.  This routine is not allowed to stall because it is called from
/// bluetooth comms code.  If the txmit queue is empty it might return an error
ErrorCode CustomRF95::send(MeshPacket *p)
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in, 
    // we almost certainly guarantee no one outside will like the packet we are sending.
    if (_mode == RHModeIdle || (_mode == RHModeRx && !_isReceiving))
    {
        // if the radio is idle, we can send right away
        DEBUG_MSG("immedate send on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", txGood(), rxGood(), rxBad());
        startSend(p);
        return ERRNO_OK;
    }
    else
    {
        DEBUG_MSG("enquing packet for send from=0x%x, to=0x%x\n", p->from, p->to);
        ErrorCode res = txQueue.enqueue(p, 0) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (res != ERRNO_OK) // we weren't able to queue it, so we must drop it to prevent leaks
            pool.release(p);

        return res;
    }
}

// After doing standard behavior, check to see if a new packet arrived or one was sent and start a new send or receive as necessary
void CustomRF95::handleInterrupt()
{
    RH_RF95::handleInterrupt();

    BaseType_t higherPriWoken = false;
    if (_mode == RHModeIdle) // We are now done sending or receiving
    {
        if (sendingPacket) // Were we sending?
        {
            // We are done sending that packet, release it
            pool.releaseFromISR(sendingPacket, &higherPriWoken);
            sendingPacket = NULL;
            // DEBUG_MSG("Done with send\n");
        }

        // If we just finished receiving a packet, forward it into a queue
        if (_rxBufValid)
        {
            // We received a packet

            // Skip the 4 headers that are at the beginning of the rxBuf
            size_t payloadLen = _bufLen - RH_RF95_HEADER_LEN;
            uint8_t *payload = _buf + RH_RF95_HEADER_LEN;

            // FIXME - throws exception if called in ISR context: frequencyError() - probably the floating point math
            int32_t freqerr = -1, snr = lastSNR();
            //DEBUG_MSG("Received packet from mesh src=0x%x,dest=0x%x,id=%d,len=%d rxGood=%d,rxBad=%d,freqErr=%d,snr=%d\n",
            //          srcaddr, destaddr, id, rxlen, rf95.rxGood(), rf95.rxBad(), freqerr, snr);

            MeshPacket *mp = pool.allocZeroed();

            SubPacket *p = &mp->payload;

            mp->from = _rxHeaderFrom;
            mp->to = _rxHeaderTo;
            //_rxHeaderId = _buf[2];
            //_rxHeaderFlags = _buf[3];

            // If we already have an entry in the DB for this nodenum, goahead and hide the snr/freqerr info there.
            // Note: we can't create it at this point, because it might be a bogus User node allocation.  But odds are we will
            // already have a record we can hide this debugging info in.
            NodeInfo *info = nodeDB.getNode(mp->from);
            if (info)
            {
                info->snr = snr;
                info->frequency_error = freqerr;
            }

            if (!pb_decode_from_bytes(payload, payloadLen, SubPacket_fields, p))
            {
                pool.releaseFromISR(mp, &higherPriWoken);
            }
            else
            {
                // parsing was successful, queue for our recipient
                mp->has_payload = true;

                int res = rxDest.enqueueFromISR(mp, &higherPriWoken); // NOWAIT - fixme, if queue is full, delete older messages
                assert(res == pdTRUE);
            }

            clearRxBuf(); // This message accepted and cleared
        }

        higherPriWoken |= handleIdleISR();
    }

    // If we call this _IT WILL NOT RETURN_
    if (higherPriWoken)
        portYIELD_FROM_ISR();
}

/** The ISR doesn't have any good work to do, give a new assignment.
 * 
 * Return true if a higher pri task has woken
 */
bool CustomRF95::handleIdleISR()
{
    BaseType_t higherPriWoken = false;

    // First send any outgoing packets we have ready
    MeshPacket *txp = txQueue.dequeuePtrFromISR(0);
    if (txp)
        startSend(txp);
    else
    {
        // Nothing to send, let's switch back to receive mode
        setModeRx();
    }

    return higherPriWoken;
}

/// This routine might be called either from user space or ISR
void CustomRF95::startSend(MeshPacket *txp)
{
    assert(!sendingPacket);

    // DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(txp->has_payload);

    size_t numbytes = pb_encode_to_bytes(radiobuf, sizeof(radiobuf), SubPacket_fields, &txp->payload);

    sendingPacket = txp;

    setHeaderTo(txp->to);
    setHeaderFrom(nodeDB.getNodeNum()); // We must do this before each send, because we might have just changed our nodenum

    // setHeaderId(0);

    assert(numbytes <= 251); // Make sure we don't overflow the tiny max packet size

    // uint32_t start = millis(); // FIXME, store this in the class

    int res = RH_RF95::send(radiobuf, numbytes);
    assert(res);
}