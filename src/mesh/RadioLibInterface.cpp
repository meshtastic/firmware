#include "RadioLibInterface.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "configuration.h"
#include "error.h"
#include "mesh-pb-constants.h"
#include <pb_decode.h>
#include <pb_encode.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

#ifdef ARCH_PORTDUINO

void LockingModule::SPItransfer(uint8_t cmd, uint8_t reg, uint8_t *dataOut, uint8_t *dataIn, uint8_t numBytes)
{
    concurrency::LockGuard g(spiLock);

    Module::SPItransfer(cmd, reg, dataOut, dataIn, numBytes);
}

#else

void LockingModule::SPIbeginTransaction()
{
    spiLock->lock();

    Module::SPIbeginTransaction();
}

void LockingModule::SPIendTransaction()
{
    spiLock->unlock();

    Module::SPIendTransaction();
}

#endif

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi, PhysicalLayer *_iface)
    : NotifiedWorkerThread("RadioIf"), module(cs, irq, rst, busy, spi, spiSettings), iface(_iface)
{
    instance = this;
#if defined(ARCH_STM32WL) && defined(USE_SX1262)
    module.setCb_digitalWrite(stm32wl_emulate_digitalWrite);
    module.setCb_digitalRead(stm32wl_emulate_digitalRead);
#endif
}

#ifdef ARCH_ESP32
// ESP32 doesn't use that flag
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR()
#else
#define YIELD_FROM_ISR(x) portYIELD_FROM_ISR(x)
#endif

void INTERRUPT_ATTR RadioLibInterface::isrLevel0Common(PendingISR cause)
{
    instance->disableInterrupt();

    BaseType_t xHigherPriorityTaskWoken;
    instance->notifyFromISR(&xHigherPriorityTaskWoken, cause, true);

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
        // If we've been trying to send the same packet more than one minute and we haven't gotten a
        // TX IRQ from the radio, the radio is probably broken.
        if (busyTx && (millis() - lastTxStart > 60000)) {
            DEBUG_MSG("Hardware Failure! busyTx for more than 60s\n");
            RECORD_CRITICALERROR(CriticalErrorCode_TransmitFailed);
#ifdef ARCH_ESP32
            if (busyTx && (millis() - lastTxStart > 65000)) // After 5s more, reboot
                ESP.restart();
#endif
        }
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

#ifndef DISABLE_WELCOME_UNSET

    if (config.lora.region != Config_LoRaConfig_RegionCode_Unset) {
        if (disabled || config.lora.tx_disabled) {

            if (config.lora.region != Config_LoRaConfig_RegionCode_Unset) {
                if (disabled || config.lora.tx_disabled) {
                    DEBUG_MSG("send - lora_tx_disabled\n");
                    packetPool.release(p);
                    return ERRNO_DISABLED;
                }

            } else {
                DEBUG_MSG("send - lora_tx_disabled because RegionCode_Unset\n");
                packetPool.release(p);
                return ERRNO_DISABLED;
            }
        }
    }

#else

    if (disabled || config.lora.tx_disabled) {
        DEBUG_MSG("send - lora_tx_disabled\n");
        packetPool.release(p);
        return ERRNO_DISABLED;
    }

#endif

        // Sometimes when testing it is useful to be able to never turn on the xmitter
#ifndef LORA_DISABLE_SENDING
        printPacket("enqueuing for send", p);

        DEBUG_MSG("txGood=%d,rxGood=%d,rxBad=%d\n", txGood, rxGood, rxBad);
        ErrorCode res = txQueue.enqueue(p) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (res != ERRNO_OK) { // we weren't able to queue it, so we must drop it to prevent leaks
            packetPool.release(p);
            return res;
        }

        // set (random) transmit delay to let others reconfigure their radio,
        // to avoid collisions and implement timing-based flooding
        // DEBUG_MSG("Set random delay before transmitting.\n");
        setTransmitDelay();

        return res;
#else
    packetPool.release(p);
    return ERRNO_DISABLED;
#endif
    }

    bool RadioLibInterface::canSleep()
    {
        bool res = txQueue.empty();
        if (!res) // only print debug messages if we are vetoing sleep
            DEBUG_MSG("radio wait to sleep, txEmpty=%d\n", res);

        return res;
    }

    /** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
    bool RadioLibInterface::cancelSending(NodeNum from, PacketId id)
    {
        auto p = txQueue.remove(from, id);
        if (p)
            packetPool.release(p); // free the packet we just removed

        bool result = (p != NULL);
        DEBUG_MSG("cancelSending id=0x%x, removed=%d\n", id, result);
        return result;
    }

    /** radio helper thread callback.
    We never immediately transmit after any operation (either Rx or Tx). Instead we should wait a random multiple of 
    'slotTimes' (see definition in RadioInterface.h) taken from a contention window (CW) to lower the chance of collision. 
    The CW size is determined by setTransmitDelay() and depends either on the current channel utilization or SNR in case 
    of a flooding message. After this, we perform channel activity detection (CAD) and reset the transmit delay if it is
    currently active. 
    */
    void RadioLibInterface::onNotify(uint32_t notification)
    {
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
            // DEBUG_MSG("delay done\n");

            // If we are not currently in receive mode, then restart the random delay (this can happen if the main thread
            // has placed the unit into standby)  FIXME, how will this work if the chipset is in sleep mode?
            if (!txQueue.empty()) {
                if (!canSendImmediately()) {
                    // DEBUG_MSG("Currently Rx/Tx-ing: set random delay\n");
                    setTransmitDelay(); // currently Rx/Tx-ing: reset random delay
                } else {
                    if (isChannelActive()) { // check if there is currently a LoRa packet on the channel
                        // DEBUG_MSG("Channel is active: set random delay\n");
                        setTransmitDelay(); // reset random delay
                    } else {
                        // Send any outgoing packets we have ready
                        MeshPacket *txp = txQueue.dequeue();
                        assert(txp);
                        startSend(txp);

                        // Packet has been sent, count it toward our TX airtime utilization.
                        uint32_t xmitMsec = getPacketTime(txp);
                        airTime->logAirtime(TX_LOG, xmitMsec);
                    }
                }
            } else {
                // DEBUG_MSG("done with txqueue\n");
            }
            break;
        default:
            assert(0); // We expected to receive a valid notification from the ISR
        }
    }

    void RadioLibInterface::setTransmitDelay()
    {
        MeshPacket *p = txQueue.getFront();
        // We want all sending/receiving to be done by our daemon thread.
        // We use a delay here because this packet might have been sent in response to a packet we just received.
        // So we want to make sure the other side has had a chance to reconfigure its radio.

        /* We assume if rx_snr = 0 and rx_rssi = 0, the packet was generated locally.
         *   This assumption is valid because of the offset generated by the radio to account for the noise
         *   floor.
         */
        if (p->rx_snr == 0 && p->rx_rssi == 0) {
            startTransmitTimer(true);
        } else {
            // If there is a SNR, start a timer scaled based on that SNR.
            DEBUG_MSG("rx_snr found. hop_limit:%d rx_snr:%f\n", p->hop_limit, p->rx_snr);
            startTransmitTimerSNR(p->rx_snr);
        }
    }

    void RadioLibInterface::startTransmitTimer(bool withDelay)
    {
        // If we have work to do and the timer wasn't already scheduled, schedule it now
        if (!txQueue.empty()) {
            uint32_t delay = !withDelay ? 1 : getTxDelayMsec();
            // DEBUG_MSG("xmit timer %d\n", delay);
            notifyLater(delay, TRANSMIT_DELAY_COMPLETED, false); // This will implicitly enable
        }
    }

    void RadioLibInterface::startTransmitTimerSNR(float snr)
    {
        // If we have work to do and the timer wasn't already scheduled, schedule it now
        if (!txQueue.empty()) {
            uint32_t delay = getTxDelayMsecWeighted(snr);
            // DEBUG_MSG("xmit timer %d\n", delay);
            notifyLater(delay, TRANSMIT_DELAY_COMPLETED, false); // This will implicitly enable
        }
    }

    void RadioLibInterface::handleTransmitInterrupt()
    {
        // DEBUG_MSG("handling lora TX interrupt\n");
        // This can be null if we forced the device to enter standby mode.  In that case
        // ignore the transmit interrupt
        if (sendingPacket)
            completeSending();
    }

    void RadioLibInterface::completeSending()
    {
        // We are careful to clear sending packet before calling printPacket because
        // that can take a long time
        auto p = sendingPacket;
        sendingPacket = NULL;

        if (p) {
            txGood++;
            printPacket("Completed sending", p);

            // We are done sending that packet, release it
            packetPool.release(p);
            // DEBUG_MSG("Done with send\n");
        }
    }

    void RadioLibInterface::handleReceiveInterrupt()
    {
        uint32_t xmitMsec;
        assert(isReceiving);
        isReceiving = false;

        // read the number of actually received bytes
        size_t length = iface->getPacketLength();

        xmitMsec = getPacketTime(length);

        int state = iface->readData(radiobuf, length);
        if (state != RADIOLIB_ERR_NONE) {
            DEBUG_MSG("ignoring received packet due to error=%d\n", state);
            rxBad++;

            airTime->logAirtime(RX_ALL_LOG, xmitMsec);

        } else {
            // Skip the 4 headers that are at the beginning of the rxBuf
            int32_t payloadLen = length - sizeof(PacketHeader);
            const uint8_t *payload = radiobuf + sizeof(PacketHeader);

            // check for short packets
            if (payloadLen < 0) {
                DEBUG_MSG("ignoring received packet too short\n");
                rxBad++;
                airTime->logAirtime(RX_ALL_LOG, xmitMsec);
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
                mp->channel = h->channel;
                assert(HOP_MAX <= PACKET_FLAGS_HOP_MASK); // If hopmax changes, carefully check this code
                mp->hop_limit = h->flags & PACKET_FLAGS_HOP_MASK;
                mp->want_ack = !!(h->flags & PACKET_FLAGS_WANT_ACK_MASK);

                addReceiveMetadata(mp);

                mp->which_payloadVariant = MeshPacket_encrypted_tag; // Mark that the payload is still encrypted at this point
                assert(((uint32_t)payloadLen) <= sizeof(mp->encrypted.bytes));
                memcpy(mp->encrypted.bytes, payload, payloadLen);
                mp->encrypted.size = payloadLen;

                printPacket("Lora RX", mp);

                // xmitMsec = getPacketTime(mp);
                airTime->logAirtime(RX_LOG, xmitMsec);

                deliverToReceiver(mp);
            }
        }
    }

    /** start an immediate transmit */
    void RadioLibInterface::startSend(MeshPacket * txp)
    {
        printPacket("Starting low level send", txp);
        if (disabled || config.lora.tx_disabled) {
            DEBUG_MSG("startSend is dropping tx packet because we are disabled\n");
            packetPool.release(txp);
        } else {
            setStandby(); // Cancel any already in process receives

            configHardwareForSend(); // must be after setStandby

            size_t numbytes = beginSending(txp);

            int res = iface->startTransmit(radiobuf, numbytes);
            if (res != RADIOLIB_ERR_NONE) {
                RECORD_CRITICALERROR(CriticalErrorCode_RadioSpiBug);

                // This send failed, but make sure to 'complete' it properly
                completeSending();
                startReceive(); // Restart receive mode (because startTransmit failed to put us in xmit mode)
            }

            // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register
            // bits
            enableInterrupt(isrTxLevel0);
        }
    }
