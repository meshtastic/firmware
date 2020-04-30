#include "CustomRF95.h"
#include "NodeDB.h" // FIXME, this class should not need to touch nodedb
#include "assert.h"
#include "configuration.h"
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef RF95_IRQ_GPIO

CustomRF95::CustomRF95() : RH_RF95(NSS_GPIO, RF95_IRQ_GPIO) {}

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

    // this->setPromiscuous(true); // Make the old RH stack work like the new one, make make CPU check dest addr
    if (ok)
        reconfigure(); // Finish our device setup

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

        if (!waitCAD())
            return false; // Check channel activity

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

// After doing standard behavior, check to see if a new packet arrived or one was sent and start a new send or receive as
// necessary
void CustomRF95::handleInterrupt()
{
    setThisAddress(
        nodeDB
            .getNodeNum()); // temp hack to make sure we are looking for the right address.  This class is going away soon anyways

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
            int32_t snr = lastSNR();
            // DEBUG_MSG("Received packet from mesh src=0x%x,dest=0x%x,id=%d,len=%d rxGood=%d,rxBad=%d,freqErr=%d,snr=%d\n",
            //          srcaddr, destaddr, id, rxlen, rf95.rxGood(), rf95.rxBad(), freqerr, snr);

            MeshPacket *mp = packetPool.allocZeroed();

            SubPacket *p = &mp->payload;

            mp->from = _rxHeaderFrom;
            mp->to = _rxHeaderTo;
            mp->id = _rxHeaderId;
            mp->rx_snr = snr;

            //_rxHeaderId = _buf[2];
            //_rxHeaderFlags = _buf[3];

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
        RH_RF95::setModeRx();
    }
}

/// This routine might be called either from user space or ISR
void CustomRF95::startSend(MeshPacket *txp)
{
    size_t numbytes = beginSending(txp);

    setHeaderTo(txp->to);
    setHeaderId(txp->id);

    // if the sender nodenum is zero, that means uninitialized
    setHeaderFrom(txp->from); // We must do this before each send, because we might have just changed our nodenum

    assert(numbytes <= 251); // Make sure we don't overflow the tiny max packet size

    // uint32_t start = millis(); // FIXME, store this in the class

    // This legacy implementation doesn't use our inserted packet header
    int res = RH_RF95::send(radiobuf + sizeof(PacketHeader), numbytes - sizeof(PacketHeader));
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
    if (lastTxStart != 0 && (now - lastTxStart) > TX_WATCHDOG_TIMEOUT && RH_RF95::mode() == RHGenericDriver::RHModeTx) {
        DEBUG_MSG("ERROR! Bug! Tx packet took too long to send, forcing radio into rx mode\n");
        RH_RF95::setModeRx();
        if (sendingPacket) { // There was probably a packet we were trying to send, free it
            packetPool.release(sendingPacket);
            sendingPacket = NULL;
        }
        recordCriticalError(ErrTxWatchdog);
        lastTxStart = 0; // Stop checking for now, because we just warned the developer
    }
}

bool CustomRF95::reconfigure()
{
    setModeIdle(); // Need to be idle before doing init

    // Set up default configuration
    // No Sync Words in LORA mode.
    setModemConfig(modemConfig); // Radio default
                                 //    setModemConfig(Bw125Cr48Sf4096); // slow and reliable?
    // rf95.setPreambleLength(8);           // Default is 8

    if (!setFrequency(freq)) {
        DEBUG_MSG("setFrequency failed\n");
        assert(0); // fixme panic
    }

    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

    // The default transmitter power is 13dBm, using PA_BOOST.
    // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
    // you can set transmitter powers from 5 to 23 dBm:
    // FIXME - can we do this?  It seems to be in the Heltec board.
    setTxPower(power, false);

    // Done with init tell radio to start receiving
    setModeRx();

    return true;
}
#endif