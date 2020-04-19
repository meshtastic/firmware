#include "CustomRF95.h"
#include "NodeDB.h" // FIXME, this class should not need to touch nodedb
#include "assert.h"
#include "configuration.h"
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef RF95_IRQ_GPIO

/// A temporary buffer used for sending/receving packets, sized to hold the biggest buffer we might need
#define MAX_RHPACKETLEN 251
static uint8_t radiobuf[MAX_RHPACKETLEN];

CustomRF95::CustomRF95() : RH_RF95(NSS_GPIO, RF95_IRQ_GPIO), txQueue(MAX_TX_QUEUE) {}

bool CustomRF95::canSleep()
{
    // We allow initializing mode, because sometimes while testing we don't ever call init() to turn on the hardware
    bool isRx = isReceiving();

    bool res = (_mode == RHModeInitialising || _mode == RHModeIdle || _mode == RHModeRx) && !isRx && txQueue.isEmpty();
    if (!res) // only print debug messages if we are vetoing sleep
        DEBUG_MSG("radio wait to sleep, mode=%d, isRx=%d, txEmpty=%d, txGood=%d\n", _mode, isRx, txQueue.isEmpty(), _txGood);

    return res;
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
    if (_mode == RHModeIdle || (_mode == RHModeRx && !isReceiving())) {
        // if the radio is idle, we can send right away
        DEBUG_MSG("immediate send on mesh fr=0x%x,to=0x%x,id=%d\n (txGood=%d,rxGood=%d,rxBad=%d)\n", p->from, p->to, p->id,
                  txGood(), rxGood(), rxBad());

        waitPacketSent(); // Make sure we dont interrupt an outgoing message

        if (!waitCAD())
            return false; // Check channel activity

        startSend(p);
        return ERRNO_OK;
    } else {
        DEBUG_MSG("enquing packet for send from=0x%x, to=0x%x\n", p->from, p->to);
        ErrorCode res = txQueue.enqueue(p, 0) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (res != ERRNO_OK) // we weren't able to queue it, so we must drop it to prevent leaks
            packetPool.release(p);

        return res;
    }
}

// After doing standard behavior, check to see if a new packet arrived or one was sent and start a new send or receive as
// necessary
void CustomRF95::handleInterrupt()
{
    RH_RF95::handleInterrupt();

    if (_mode == RHModeIdle) // We are now done sending or receiving
    {
        if (sendingPacket) // Were we sending?
        {
            // We are done sending that packet, release it
            packetPool.release(sendingPacket);
            sendingPacket = NULL;
            // DEBUG_MSG("Done with send\n");
        }

        // If we just finished receiving a packet, forward it into a queue
        if (_rxBufValid) {
            // We received a packet

            // Skip the 4 headers that are at the beginning of the rxBuf
            size_t payloadLen = _bufLen - RH_RF95_HEADER_LEN;
            uint8_t *payload = _buf + RH_RF95_HEADER_LEN;

            // FIXME - throws exception if called in ISR context: frequencyError() - probably the floating point math
            int32_t freqerr = -1, snr = lastSNR();
            // DEBUG_MSG("Received packet from mesh src=0x%x,dest=0x%x,id=%d,len=%d rxGood=%d,rxBad=%d,freqErr=%d,snr=%d\n",
            //          srcaddr, destaddr, id, rxlen, rf95.rxGood(), rf95.rxBad(), freqerr, snr);

            MeshPacket *mp = packetPool.allocZeroed();

            SubPacket *p = &mp->payload;

            mp->from = _rxHeaderFrom;
            mp->to = _rxHeaderTo;
            mp->id = _rxHeaderId;

            //_rxHeaderId = _buf[2];
            //_rxHeaderFlags = _buf[3];

            // If we already have an entry in the DB for this nodenum, goahead and hide the snr/freqerr info there.
            // Note: we can't create it at this point, because it might be a bogus User node allocation.  But odds are we will
            // already have a record we can hide this debugging info in.
            NodeInfo *info = nodeDB.getNode(mp->from);
            if (info) {
                info->snr = snr;
                info->frequency_error = freqerr;
            }

            if (!pb_decode_from_bytes(payload, payloadLen, SubPacket_fields, p)) {
                packetPool.release(mp);
            } else {
                // parsing was successful, queue for our recipient
                mp->has_payload = true;

                deliverToReceiver(mp);
            }

            clearRxBuf(); // This message accepted and cleared
        }

        handleIdleISR();
    }
}

/** The ISR doesn't have any good work to do, give a new assignment.
 *
 * Return true if a higher pri task has woken
 */
void CustomRF95::handleIdleISR()
{
    // First send any outgoing packets we have ready
    MeshPacket *txp = txQueue.dequeuePtr(0);
    if (txp)
        startSend(txp);
    else {
        // Nothing to send, let's switch back to receive mode
        setModeRx();
    }
}

/// This routine might be called either from user space or ISR
void CustomRF95::startSend(MeshPacket *txp)
{
    assert(!sendingPacket);

    // DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(txp->has_payload);

    lastTxStart = millis();

    size_t numbytes = pb_encode_to_bytes(radiobuf, sizeof(radiobuf), SubPacket_fields, &txp->payload);

    sendingPacket = txp;

    setHeaderTo(txp->to);
    setHeaderId(txp->id);

    // if the sender nodenum is zero, that means uninitialized
    assert(txp->from);
    setHeaderFrom(txp->from); // We must do this before each send, because we might have just changed our nodenum

    assert(numbytes <= 251); // Make sure we don't overflow the tiny max packet size

    // uint32_t start = millis(); // FIXME, store this in the class

    int res = RH_RF95::send(radiobuf, numbytes);
    assert(res);
}

#define TX_WATCHDOG_TIMEOUT 30 * 1000

#include "error.h"

void CustomRF95::loop()
{
    RH_RF95::loop();

    // It should never take us more than 30 secs to send a packet, if it does, we have a bug, FIXME, move most of this
    // into CustomRF95
    uint32_t now = millis();
    if (lastTxStart != 0 && (now - lastTxStart) > TX_WATCHDOG_TIMEOUT && mode() == RHGenericDriver::RHModeTx) {
        DEBUG_MSG("ERROR! Bug! Tx packet took too long to send, forcing radio into rx mode\n");
        setModeRx();
        if (sendingPacket) { // There was probably a packet we were trying to send, free it
            packetPool.release(sendingPacket);
            sendingPacket = NULL;
        }
        recordCriticalError(ErrTxWatchdog);
        lastTxStart = 0; // Stop checking for now, because we just warned the developer
    }
}

#endif