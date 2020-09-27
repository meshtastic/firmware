#include "RadioLibInterface.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "mesh-pb-constants.h"
#include <configuration.h>
#include <pb_decode.h>
#include <pb_encode.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

void LockingModule::SPItransfer(uint8_t cmd, uint8_t reg, uint8_t *dataOut, uint8_t *dataIn, uint8_t numBytes)
{
    concurrency::LockGuard g(spiLock);

    Module::SPItransfer(cmd, reg, dataOut, dataIn, numBytes);
}

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi, PhysicalLayer *_iface)
    : concurrency::PeriodicTask(0), module(cs, irq, rst, busy, spi, spiSettings), iface(_iface)
{
    instance = this;
}

bool RadioLibInterface::init()
{
    setup(); // init our timer
    return RadioInterface::init();
}

#ifndef NO_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

void INTERRUPT_ATTR RadioLibInterface::isrLevel0Common(PendingISR cause)
{
    instance->disableInterrupt();

    instance->pending = cause;
    BaseType_t xHigherPriorityTaskWoken;
    instance->notifyFromISR(&xHigherPriorityTaskWoken, cause, eSetValueWithOverwrite);

    /* Force a context switch if xHigherPriorityTaskWoken is now set to pdTRUE.
    The macro used to do this is dependent on the port and may be called
    portEND_SWITCHING_ISR. */
    YIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void INTERRUPT_ATTR RadioLibInterface::isrRxLevel0()
{
    isrLevel0Common(ISR_RX);
}

void INTERRUPT_ATTR RadioLibInterface::isrTxLevel0()
{
    isrLevel0Common(ISR_TX);
}

/** Our ISR code currently needs this to find our active instance
 */
RadioLibInterface *RadioLibInterface::instance;

/**
 * Convert our modemConfig enum into wf, sf, etc...
 */
void RadioLibInterface::applyModemConfig()
{
    RadioInterface::applyModemConfig();

    if (channelSettings.spread_factor == 0) {
        switch (channelSettings.modem_config) {
        case ChannelSettings_ModemConfig_Bw125Cr45Sf128: ///< Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Default medium
                                                         ///< range
            bw = 125;
            cr = 5;
            sf = 7;
            break;
        case ChannelSettings_ModemConfig_Bw500Cr45Sf128: ///< Bw = 500 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Fast+short
                                                         ///< range
            bw = 500;
            cr = 5;
            sf = 7;
            break;
        case ChannelSettings_ModemConfig_Bw31_25Cr48Sf512: ///< Bw = 31.25 kHz, Cr = 4/8, Sf = 512chips/symbol, CRC on. Slow+long
                                                           ///< range
            bw = 31.25;
            cr = 8;
            sf = 9;
            break;
        case ChannelSettings_ModemConfig_Bw125Cr48Sf4096:
            bw = 125;
            cr = 8;
            sf = 12;
            break;
        default:
            assert(0); // Unknown enum
        }
    } else {
        sf = channelSettings.spread_factor;
        cr = channelSettings.coding_rate;
        bw = channelSettings.bandwidth;

        if (bw == 31) // This parameter is not an integer
            bw = 31.25;
    }
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
bool RadioLibInterface::canSendImmediately()
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    bool busyTx = sendingPacket != NULL;
    bool busyRx = isReceiving && isActivelyReceiving();

    if (busyTx || busyRx) {
        if (busyTx)
            DEBUG_MSG("Can not send yet, busyTx\n");
        if (busyRx)
            DEBUG_MSG("Can not send yet, busyRx\n");
        return false;
    } else
        return true;
}

/// Send a packet (possibly by enquing in a private fifo).  This routine will
/// later free() the packet to pool.  This routine is not allowed to stall because it is called from
/// bluetooth comms code.  If the txmit queue is empty it might return an error
ErrorCode RadioLibInterface::send(MeshPacket *p)
{
    // Sometimes when testing it is useful to be able to never turn on the xmitter
#ifndef LORA_DISABLE_SENDING
    printPacket("enqueuing for send", p);
    DEBUG_MSG("txGood=%d,rxGood=%d,rxBad=%d\n", txGood, rxGood, rxBad);
    ErrorCode res = txQueue.enqueue(p, 0) ? ERRNO_OK : ERRNO_UNKNOWN;

    if (res != ERRNO_OK) { // we weren't able to queue it, so we must drop it to prevent leaks
        packetPool.release(p);
        return res;
    }

    // We want all sending/receiving to be done by our daemon thread, We use a delay here because this packet might have been sent
    // in response to a packet we just received.  So we want to make sure the other side has had a chance to reconfigure its radio
    startTransmitTimer(true);

    return res;
#else
    packetPool.release(p);
    return ERRNO_UNKNOWN;
#endif
}

bool RadioLibInterface::canSleep()
{
    bool res = txQueue.isEmpty();
    if (!res) // only print debug messages if we are vetoing sleep
        DEBUG_MSG("radio wait to sleep, txEmpty=%d\n", res);

    return res;
}

/** At the low end we want to pick a delay large enough that anyone who just completed sending (some other node)
 * has had enough time to switch their radio back into receive mode.
 */
#define MIN_TX_WAIT_MSEC 100

/**
 * At the high end, this value is used to spread node attempts across time so when they are replying to a packet
 * they don't both check that the airwaves are clear at the same moment.  As long as they are off by some amount
 * one of the two will be first to start transmitting and the other will see that.  I bet 500ms is more than enough
 * to guarantee this.
 */
#define MAX_TX_WAIT_MSEC 2000 // stress test would still fail occasionally with 1000

/** radio helper thread callback.

We never immediately transmit after any operation (either rx or tx).  Instead we should start receiving and
wait a random delay of 50 to 200 ms to make sure we are not stomping on someone else.  The 50ms delay at the beginning ensures all
possible listeners have had time to finish processing the previous packet and now have their radio in RX state.  The up to 200ms
random delay gives a chance for all possible senders to have high odds of detecting that someone else started transmitting first
and then they will wait until that packet finishes.

NOTE: the large flood rebroadcast delay might still be needed even with this approach.  Because we might not be able to hear other
transmitters that we are potentially stomping on.  Requires further thought.

FIXME, the MIN_TX_WAIT_MSEC and MAX_TX_WAIT_MSEC values should be tuned via logic analyzer later.
*/
void RadioLibInterface::loop()
{
    pending = ISR_NONE;

    switch (notification) {
    case ISR_TX:
        handleTransmitInterrupt();
        startReceive();
        // DEBUG_MSG("tx complete - starting timer\n");
        startTransmitTimer();
        break;
    case ISR_RX:
        handleReceiveInterrupt();
        startReceive();
        // DEBUG_MSG("rx complete - starting timer\n");
        startTransmitTimer();
        break;
    case TRANSMIT_DELAY_COMPLETED:
        // If we are not currently in receive mode, then restart the timer and try again later (this can happen if the main thread
        // has placed the unit into standby)  FIXME, how will this work if the chipset is in sleep mode?
        if (!txQueue.isEmpty()) {
            if (!canSendImmediately()) {
                startTransmitTimer(); // try again in a little while
            } else {
                // Send any outgoing packets we have ready
                MeshPacket *txp = txQueue.dequeuePtr(0);
                assert(txp);
                startSend(txp);
            }
        } else {
            // DEBUG_MSG("done with txqueue\n");
        }
        break;
    default:
        assert(0); // We expected to receive a valid notification from the ISR
    }
}

void RadioLibInterface::doTask()
{
    disable(); // Don't call this callback again

    // We use without overwrite, so that if there is already an interrupt pending to be handled, that gets handle properly (the
    // ISR handler will restart our timer)

    notify(TRANSMIT_DELAY_COMPLETED, eSetValueWithoutOverwrite);
}

void RadioLibInterface::startTransmitTimer(bool withDelay)
{
    // If we have work to do and the timer wasn't already scheduled, schedule it now
    if (getPeriod() == 0 && !txQueue.isEmpty()) {
        uint32_t delay =
            !withDelay ? 1 : random(MIN_TX_WAIT_MSEC, MAX_TX_WAIT_MSEC); // See documentation for loop() wrt these values
                                                                         // DEBUG_MSG("xmit timer %d\n", delay);
        // DEBUG_MSG("delaying %u\n", delay);
        setPeriod(delay);
    }
}

void RadioLibInterface::handleTransmitInterrupt()
{
    // DEBUG_MSG("handling lora TX interrupt\n");
    assert(sendingPacket); // Were we sending? - FIXME, this was null coming out of light sleep due to RF95 ISR!

    completeSending();
}

void RadioLibInterface::completeSending()
{
    if (sendingPacket) {
        txGood++;
        printPacket("Completed sending", sendingPacket);

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

            rxGood++;

            // Note: we deliver _all_ packets to our router (i.e. our interface is intentionally promiscuous).
            // This allows the router and other apps on our node to sniff packets (usually routing) between other
            // nodes.
            MeshPacket *mp = packetPool.allocZeroed();

            mp->from = h->from;
            mp->to = h->to;
            mp->id = h->id;
            assert(HOP_MAX <= PACKET_FLAGS_HOP_MASK); // If hopmax changes, carefully check this code
            mp->hop_limit = h->flags & PACKET_FLAGS_HOP_MASK;
            mp->want_ack = !!(h->flags & PACKET_FLAGS_WANT_ACK_MASK);

            addReceiveMetadata(mp);

            mp->which_payload = MeshPacket_encrypted_tag; // Mark that the payload is still encrypted at this point
            assert(payloadLen <= sizeof(mp->encrypted.bytes));
            memcpy(mp->encrypted.bytes, payload, payloadLen);
            mp->encrypted.size = payloadLen;

            printPacket("Lora RX", mp);

            deliverToReceiver(mp);
        }
    }
}

/** start an immediate transmit */
void RadioLibInterface::startSend(MeshPacket *txp)
{
    printPacket("Starting low level send", txp);
    setStandby(); // Cancel any already in process receives

    configHardwareForSend(); // must be after setStandby

    size_t numbytes = beginSending(txp);

    int res = iface->startTransmit(radiobuf, numbytes);
    assert(res == ERR_NONE);

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrTxLevel0);
}
