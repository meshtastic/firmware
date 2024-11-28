#include "RadioLibInterface.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "SPILock.h"
#include "Throttle.h"
#include "configuration.h"
#include "error.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include <pb_decode.h>
#include <pb_encode.h>

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#include "meshUtils.h"
#endif
void LockingArduinoHal::spiBeginTransaction()
{
    spiLock->lock();

    ArduinoHal::spiBeginTransaction();
}

void LockingArduinoHal::spiEndTransaction()
{
    ArduinoHal::spiEndTransaction();

    spiLock->unlock();
}
#if ARCH_PORTDUINO
void LockingArduinoHal::spiTransfer(uint8_t *out, size_t len, uint8_t *in)
{
    if (busy == RADIOLIB_NC) {
        spi->transfer(out, in, len);
    } else {
        uint16_t offset = 0;

        while (len) {
            uint8_t block_size = (len < 20 ? len : 20);
            spi->transfer((out != NULL ? out + offset : NULL), (in != NULL ? in + offset : NULL), block_size);
            if (block_size == len)
                return;

            // ensure GPIO is low

            uint32_t start = millis();
            while (digitalRead(busy)) {
                if (!Throttle::isWithinTimespanMs(start, 2000)) {
                    LOG_ERROR("GPIO mid-transfer timeout, is it connected?");
                    return;
                }
            }

            offset += block_size;
            len -= block_size;
        }
    }
}
#endif

RadioLibInterface::RadioLibInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                     RADIOLIB_PIN_TYPE busy, PhysicalLayer *_iface)
    : NotifiedWorkerThread("RadioIf"), module(hal, cs, irq, rst, busy), iface(_iface)
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

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
bool RadioLibInterface::canSendImmediately()
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    bool busyTx = sendingPacket != NULL;
    bool busyRx = isReceiving && isActivelyReceiving();

    if (busyTx || busyRx) {
        if (busyTx) {
            LOG_WARN("Can not send yet, busyTx");
        }
        // If we've been trying to send the same packet more than one minute and we haven't gotten a
        // TX IRQ from the radio, the radio is probably broken.
        if (busyTx && !Throttle::isWithinTimespanMs(lastTxStart, 60000)) {
            LOG_ERROR("Hardware Failure! busyTx for more than 60s");
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_TRANSMIT_FAILED);
            // reboot in 5 seconds when this condition occurs.
            rebootAtMsec = lastTxStart + 65000;
        }
        if (busyRx) {
            LOG_WARN("Can not send yet, busyRx");
        }
        return false;
    } else
        return true;
}

bool RadioLibInterface::receiveDetected(uint16_t irq, ulong syncWordHeaderValidFlag, ulong preambleDetectedFlag)
{
    bool detected = (irq & (syncWordHeaderValidFlag | preambleDetectedFlag));
    // Handle false detections
    if (detected) {
        if (!activeReceiveStart) {
            activeReceiveStart = millis();
        } else if (!Throttle::isWithinTimespanMs(activeReceiveStart, 2 * preambleTimeMsec) && !(irq & syncWordHeaderValidFlag)) {
            // The HEADER_VALID flag should be set by now if it was really a packet, so ignore PREAMBLE_DETECTED flag
            activeReceiveStart = 0;
            LOG_DEBUG("Ignore false preamble detection");
            return false;
        } else if (!Throttle::isWithinTimespanMs(activeReceiveStart, maxPacketTimeMsec)) {
            // We should have gotten an RX_DONE IRQ by now if it was really a packet, so ignore HEADER_VALID flag
            activeReceiveStart = 0;
            LOG_DEBUG("Ignore false header detection");
            return false;
        }
    }
    return detected;
}

/// Send a packet (possibly by enquing in a private fifo).  This routine will
/// later free() the packet to pool.  This routine is not allowed to stall because it is called from
/// bluetooth comms code.  If the txmit queue is empty it might return an error
ErrorCode RadioLibInterface::send(meshtastic_MeshPacket *p)
{

#ifndef DISABLE_WELCOME_UNSET

    if (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        if (disabled || !config.lora.tx_enabled) {
            LOG_WARN("send - !config.lora.tx_enabled");
            packetPool.release(p);
            return ERRNO_DISABLED;
        }

    } else {
        LOG_WARN("send - lora tx disabled: Region unset");
        packetPool.release(p);
        return ERRNO_DISABLED;
    }

#else

    if (disabled || !config.lora.tx_enabled) {
        LOG_WARN("send - !config.lora.tx_enabled");
        packetPool.release(p);
        return ERRNO_DISABLED;
    }

#endif

    if (p->to == NODENUM_BROADCAST_NO_LORA) {
        LOG_DEBUG("Drop no-LoRa pkt");
        return ERRNO_SHOULD_RELEASE;
    }

    // Sometimes when testing it is useful to be able to never turn on the xmitter
#ifndef LORA_DISABLE_SENDING
    printPacket("enqueue for send", p);

    LOG_DEBUG("txGood=%d,txRelay=%d,rxGood=%d,rxBad=%d", txGood, txRelay, rxGood, rxBad);
    ErrorCode res = txQueue.enqueue(p) ? ERRNO_OK : ERRNO_UNKNOWN;

    if (res != ERRNO_OK) { // we weren't able to queue it, so we must drop it to prevent leaks
        packetPool.release(p);
        return res;
    }

    // set (random) transmit delay to let others reconfigure their radio,
    // to avoid collisions and implement timing-based flooding
    setTransmitDelay();

    return res;
#else
    packetPool.release(p);
    return ERRNO_DISABLED;
#endif
}

meshtastic_QueueStatus RadioLibInterface::getQueueStatus()
{
    meshtastic_QueueStatus qs;

    qs.res = qs.mesh_packet_id = 0;
    qs.free = txQueue.getFree();
    qs.maxlen = txQueue.getMaxLen();

    return qs;
}

bool RadioLibInterface::canSleep()
{
    bool res = txQueue.empty();
    if (!res) { // only print debug messages if we are vetoing sleep
        LOG_DEBUG("Radio wait to sleep, txEmpty=%d", res);
    }
    return res;
}

/** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
bool RadioLibInterface::cancelSending(NodeNum from, PacketId id)
{
    auto p = txQueue.remove(from, id);
    if (p)
        packetPool.release(p); // free the packet we just removed

    bool result = (p != NULL);
    LOG_DEBUG("cancelSending id=0x%x, removed=%d", id, result);
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
        startTransmitTimer();
        break;
    case ISR_RX:
        handleReceiveInterrupt();
        startReceive();
        startTransmitTimer();
        break;
    case TRANSMIT_DELAY_COMPLETED:

        // If we are not currently in receive mode, then restart the random delay (this can happen if the main thread
        // has placed the unit into standby)  FIXME, how will this work if the chipset is in sleep mode?
        if (!txQueue.empty()) {
            if (!canSendImmediately()) {
                setTransmitDelay(); // currently Rx/Tx-ing: reset random delay
            } else {
                if (isChannelActive()) { // check if there is currently a LoRa packet on the channel
                    startReceive();      // try receiving this packet, afterwards we'll be trying to transmit again
                    setTransmitDelay();
                } else {
                    // Send any outgoing packets we have ready as fast as possible to keep the time between channel scan and
                    // actual transmission as short as possible
                    meshtastic_MeshPacket *txp = txQueue.dequeue();
                    assert(txp);
                    bool sent = startSend(txp);
                    if (sent) {
                        // Packet has been sent, count it toward our TX airtime utilization.
                        uint32_t xmitMsec = getPacketTime(txp);
                        airTime->logAirtime(TX_LOG, xmitMsec);
                    }
                }
            }
        } else {
        }
        break;
    default:
        assert(0); // We expected to receive a valid notification from the ISR
    }
}

void RadioLibInterface::setTransmitDelay()
{
    meshtastic_MeshPacket *p = txQueue.getFront();
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
        LOG_DEBUG("rx_snr found. hop_limit:%d rx_snr:%f", p->hop_limit, p->rx_snr);
        startTransmitTimerSNR(p->rx_snr);
    }
}

void RadioLibInterface::startTransmitTimer(bool withDelay)
{
    // If we have work to do and the timer wasn't already scheduled, schedule it now
    if (!txQueue.empty()) {
        uint32_t delay = !withDelay ? 1 : getTxDelayMsec();
        notifyLater(delay, TRANSMIT_DELAY_COMPLETED, false); // This will implicitly enable
    }
}

void RadioLibInterface::startTransmitTimerSNR(float snr)
{
    // If we have work to do and the timer wasn't already scheduled, schedule it now
    if (!txQueue.empty()) {
        uint32_t delay = getTxDelayMsecWeighted(snr);
        notifyLater(delay, TRANSMIT_DELAY_COMPLETED, false); // This will implicitly enable
    }
}

void RadioLibInterface::handleTransmitInterrupt()
{
    // This can be null if we forced the device to enter standby mode.  In that case
    // ignore the transmit interrupt
    if (sendingPacket)
        completeSending();
    powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn); // But our transmitter is definitely off now
}

void RadioLibInterface::completeSending()
{
    // We are careful to clear sending packet before calling printPacket because
    // that can take a long time
    auto p = sendingPacket;
    sendingPacket = NULL;

    if (p) {
        txGood++;
        if (!isFromUs(p))
            txRelay++;
        printPacket("Completed sending", p);

        // We are done sending that packet, release it
        packetPool.release(p);
    }
}

void RadioLibInterface::handleReceiveInterrupt()
{
    uint32_t xmitMsec;

    // when this is called, we should be in receive mode - if we are not, just jump out instead of bombing. Possible Race
    // Condition?
    if (!isReceiving) {
        LOG_ERROR("handleReceiveInterrupt called when not in rx mode, which shouldn't happen");
        return;
    }

    isReceiving = false;

    // read the number of actually received bytes
    size_t length = iface->getPacketLength();

    xmitMsec = getPacketTime(length);

#ifndef DISABLE_WELCOME_UNSET
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        LOG_WARN("lora rx disabled: Region unset");
        airTime->logAirtime(RX_ALL_LOG, xmitMsec);
        return;
    }
#endif

    int state = iface->readData((uint8_t *)&radioBuffer, length);
#if ARCH_PORTDUINO
    if (settingsMap[logoutputlevel] == level_trace) {
        printBytes("Raw incoming packet: ", (uint8_t *)&radioBuffer, length);
    }
#endif
    if (state != RADIOLIB_ERR_NONE) {
        LOG_ERROR("Ignore received packet due to error=%d", state);
        rxBad++;

        airTime->logAirtime(RX_ALL_LOG, xmitMsec);

    } else {
        // Skip the 4 headers that are at the beginning of the rxBuf
        int32_t payloadLen = length - sizeof(PacketHeader);

        // check for short packets
        if (payloadLen < 0) {
            LOG_WARN("Ignore received packet too short");
            rxBad++;
            airTime->logAirtime(RX_ALL_LOG, xmitMsec);
        } else {
            rxGood++;
            // altered packet with "from == 0" can do Remote Node Administration without permission
            if (radioBuffer.header.from == 0) {
                LOG_WARN("Ignore received packet without sender");
                return;
            }

            // Note: we deliver _all_ packets to our router (i.e. our interface is intentionally promiscuous).
            // This allows the router and other apps on our node to sniff packets (usually routing) between other
            // nodes.
            meshtastic_MeshPacket *mp = packetPool.allocZeroed();

            mp->from = radioBuffer.header.from;
            mp->to = radioBuffer.header.to;
            mp->id = radioBuffer.header.id;
            mp->channel = radioBuffer.header.channel;
            assert(HOP_MAX <= PACKET_FLAGS_HOP_LIMIT_MASK); // If hopmax changes, carefully check this code
            mp->hop_limit = radioBuffer.header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
            mp->hop_start = (radioBuffer.header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
            mp->want_ack = !!(radioBuffer.header.flags & PACKET_FLAGS_WANT_ACK_MASK);
            mp->via_mqtt = !!(radioBuffer.header.flags & PACKET_FLAGS_VIA_MQTT_MASK);

            addReceiveMetadata(mp);

            mp->which_payload_variant =
                meshtastic_MeshPacket_encrypted_tag; // Mark that the payload is still encrypted at this point
            assert(((uint32_t)payloadLen) <= sizeof(mp->encrypted.bytes));
            memcpy(mp->encrypted.bytes, radioBuffer.payload, payloadLen);
            mp->encrypted.size = payloadLen;

            printPacket("Lora RX", mp);

            airTime->logAirtime(RX_LOG, xmitMsec);

            deliverToReceiver(mp);
        }
    }
}

void RadioLibInterface::startReceive()
{
    isReceiving = true;
    powerMon->setState(meshtastic_PowerMon_State_Lora_RXOn);
}

void RadioLibInterface::configHardwareForSend()
{
    powerMon->setState(meshtastic_PowerMon_State_Lora_TXOn);
}

void RadioLibInterface::setStandby()
{
    // neither sending nor receiving
    powerMon->clearState(meshtastic_PowerMon_State_Lora_RXOn);
    powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn);
}

/** start an immediate transmit */
bool RadioLibInterface::startSend(meshtastic_MeshPacket *txp)
{
    /* NOTE: Minimize the actions before startTransmit() to keep the time between
             channel scan and actual transmit as low as possible to avoid collisions. */
    if (disabled || !config.lora.tx_enabled) {
        LOG_WARN("Drop Tx packet because LoRa Tx disabled");
        packetPool.release(txp);
        return false;
    } else {
        configHardwareForSend(); // must be after setStandby

        size_t numbytes = beginSending(txp);

        int res = iface->startTransmit((uint8_t *)&radioBuffer, numbytes);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("startTransmit failed, error=%d", res);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_RADIO_SPI_BUG);

            // This send failed, but make sure to 'complete' it properly
            completeSending();
            powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn); // Transmitter off now
            startReceive(); // Restart receive mode (because startTransmit failed to put us in xmit mode)
        } else {
            lastTxStart = millis();
            printPacket("Started Tx", txp);
        }

        // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register
        // bits
        enableInterrupt(isrTxLevel0);

        return res == RADIOLIB_ERR_NONE;
    }
}