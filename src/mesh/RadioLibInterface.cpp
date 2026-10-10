#include "RadioLibInterface.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "RadioTxHook.h"
#include "SPILock.h"
#include "Throttle.h"
#include "UptimeClock.h"
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
    spi->transfer(out, in, len);
}
#endif

RadioLibInterface::RadioLibInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                     RADIOLIB_PIN_TYPE busy, PhysicalLayer *_iface)
    : NotifiedWorkerThread("RadioIf"), module(hal, cs, irq, rst, busy), iface(_iface)
{
    instance = this;

    // Initialize unused sample slots to a sane default; sample count controls averaging.
    for (uint8_t i = 0; i < NOISE_FLOOR_SAMPLES; i++) {
        noiseFloorSamples[i] = NOISE_FLOOR_DEFAULT;
    }

#if defined(ARCH_STM32WL) && defined(USE_SX1262)
    module.setCb_digitalWrite(stm32wl_emulate_digitalWrite);
    module.setCb_digitalRead(stm32wl_emulate_digitalRead);
#endif
}

uint32_t RadioLibInterface::tcxoStartupDelayUs()
{
#if ARCH_PORTDUINO
    // 0 is an absent key; the build default applies
    if (portduino_config.dio3_tcxo_delay_us > 0)
        return (uint32_t)portduino_config.dio3_tcxo_delay_us;
    if (portduino_config.dio3_tcxo_delay_us < 0)
        LOG_WARN("Ignore Lora.DIO3_TCXO_DELAY_US %d, use %u us", portduino_config.dio3_tcxo_delay_us,
                 (unsigned)TCXO_STARTUP_DELAY_US);
#endif
    return TCXO_STARTUP_DELAY_US;
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
    // With a readout task, it takes the frame out of the radio, and the interrupt stays enabled for the next one
    if (instance->rxDoneFromIsr())
        return;
    isrLevel0Common(ISR_RX);
}

void INTERRUPT_ATTR RadioLibInterface::isrTxLevel0()
{
    // Before the notify: the handler that would otherwise re-arm RX can wait behind a main-loop hold.
    instance->rearmReceiveFromIsr();
    isrLevel0Common(ISR_TX);
}

/** Our ISR code currently needs this to find our active instance
 */
RadioLibInterface *RadioLibInterface::instance;

/** At most one busyRx deferral line per this interval; the line carries the count it stands for. */
#define BUSY_RX_LOG_INTERVAL_MS 30000

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
bool RadioLibInterface::canSendImmediately()
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    bool busyTx = sendingPacket != NULL;
    // isActivelyReceiving() reads the chip over SPI. Unlocked, it can split the readout task's command and wedge BUSY for
    // RadioLib's whole BUSY timeout. The lock is recursive, so a caller already holding it re-enters.
    bool busyRx = false;
    if (isReceiving) {
        RadioSequence seq(this);
        busyRx = isActivelyReceiving();
    }

    if (busyTx || busyRx) {
        if (busyTx) {
            LOG_WARN("Can not send yet, busyTx");
        }
        // If we've been trying to send the same packet more than one minute and we haven't gotten a
        // TX IRQ from the radio, the radio is probably broken.
        if (busyTx && !Throttle::isWithinTimespanMs(lastTxStart, 60000)) {
            LOG_ERROR("Hardware Failure! busyTx >60s");
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_TRANSMIT_FAILED);
            // reboot in 5 seconds when this condition occurs.
            rebootAtMsec = Time::skipZero(lastTxStart + 65000);
        }
        if (busyRx) {
            // Normal on a busy channel, and checked on every attempt: log the count at most once per interval
            busyRxDeferred++;
            const uint32_t nowMs = Time::getMillis();
            if (lastBusyRxLogMs == 0 || Throttle::hasElapsed(lastBusyRxLogMs, BUSY_RX_LOG_INTERVAL_MS)) {
                LOG_WARN("Can not send yet, busyRx (%u deferred in %u ms)", (unsigned)busyRxDeferred,
                         (unsigned)(lastBusyRxLogMs ? nowMs - lastBusyRxLogMs : nowMs));
                busyRxDeferred = 0;
                lastBusyRxLogMs = Time::skipZero(nowMs);
            }
        }
        return false;
    } else
        return true;
}

bool RadioLibInterface::preambleHoldActive()
{
    // Whatever sent the cleared preamble is off the air one max packet later.
    if (preambleHoldStart && !Throttle::isWithinTimespanMs(
                                 preambleHoldStart, getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader))))
        preambleHoldStart = 0;
    return preambleHoldStart != 0;
}

void RadioLibInterface::holdOnPreamble()
{
    // During a hold a refire stays latched, so the first look after it sees an external source and holds again.
    if (preambleHoldActive())
        return;
    iface->clearIrq(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    preambleHoldStart = Time::skipZero(Time::getMillis());
    LOG_TRACE("Preamble seen, cleared, holding TX");
}

bool RadioLibInterface::receiveDetected(uint16_t irq, unsigned long syncWordHeaderValidFlag, unsigned long preambleDetectedFlag)
{
    if (preambleHoldActive())
        return true;

    if (irq & syncWordHeaderValidFlag) {
        if (!activeReceiveStart) {
            activeReceiveStart = Time::skipZero(Time::getMillis());
        } else if (!Throttle::isWithinTimespanMs(activeReceiveStart,
                                                 getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader)))) {
            // We should have gotten an RX_DONE IRQ by now if it was really a packet, so ignore HEADER_VALID flag
            activeReceiveStart = 0;
            LOG_TRACE("Ignore false header detection");
            return false;
        }
        return true;
    }

    if (irq & preambleDetectedFlag) {
        // Looks come once per CSMA backoff, too rarely to judge a preamble by symbol-time deadline (#11933).
        // Clear it so the next look sees only a fresh one, and hold TX meanwhile; a clear never aborts RX.
        holdOnPreamble();
        return true;
    }
    return false;
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
        LOG_DEBUG_RADIO("Drop no-LoRa pkt");
        return ERRNO_SHOULD_RELEASE;
    }

    // Sometimes when testing it is useful to be able to never turn on the xmitter
#ifndef LORA_DISABLE_SENDING
    printPacket("enqueue for send", p);

    LOG_TRACE("txGood=%d,txRelay=%d,rxGood=%d,rxBad=%d", txGood, txRelay, rxGood, rxBad);
    bool dropped = false;
    ErrorCode res = txQueue.enqueue(p, &dropped) ? ERRNO_OK : ERRNO_UNKNOWN;

    if (dropped) {
        txDrop++;
    }

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

bool RadioLibInterface::canSleep(bool deepSleep)
{
    // A packet being actively transmitted has already left the TX queue (sendingPacket), so
    // check it separately. It only vetoes deep sleep: light sleep keeps the radio powered and
    // the TX finishes on its own, but deep sleep powers the radio down and would truncate the
    // packet on air.
    bool res = txQueue.empty() && !(deepSleep && isSending());
    if (!res) { // only print debug messages if we are vetoing sleep
        LOG_DEBUG_RADIO("Radio wait to sleep, txEmpty=%d, txInFlight=%d", txQueue.empty(), isSending());
    }
    return res;
}

/** Allow other firmware components to ask whether we are currently sending a packet
Initially implemented to protect T-Echo's capacitive touch button from spurious presses during tx
*/
bool RadioLibInterface::isSending()
{
    return sendingPacket != NULL;
}

/** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
bool RadioLibInterface::cancelSending(NodeNum from, PacketId id)
{
    auto p = txQueue.remove(from, id);
    if (p) {
        RadioTxHooks::packetReleased(this, p);
        packetPool.release(p); // free the packet we just removed
    }

    bool result = (p != NULL);
    LOG_DEBUG_RADIO("cancelSending id=0x%08x, removed=%d", id, result);
    return result;
}

/** Attempt to find a packet in the TxQueue. Returns true if the packet was found. */
bool RadioLibInterface::findInTxQueue(NodeNum from, PacketId id)
{
    return txQueue.find(from, id);
}

void RadioLibInterface::updateNoiseFloor()
{
    // Only sample from idle receive mode. TX/RX-critical paths must return to radio work quickly.
    if (!isReceiving || sendingPacket != NULL) {
        return;
    }
    // Ahead of the lock and the chip reads below, which then cost nothing on the iterations this skips
    if (Throttle::isWithinTimespanMs(lastNoiseFloorUpdate, NOISE_FLOOR_UPDATE_INTERVAL_MS)) {
        return;
    }

    int16_t rssi;
    {
        // Three chip reads from the main loop, which holds no sequence of its own: unlocked, the readout task can land
        // between them and wedge BUSY for RadioLib's whole timeout, same as canSendImmediately() above.
        RadioSequence seq(this);
        if (isActivelyReceiving() || isIRQPending()) {
            return;
        }
        lastNoiseFloorUpdate = Time::getMillis();
        rssi = getCurrentRSSI();
    }
    if (rssi == NOISE_FLOOR_INVALID || rssi >= 0 || rssi < NOISE_FLOOR_VALID_MIN) {
        LOG_DEBUG_RADIO("Skipping invalid RSSI reading: %d", rssi);
        return;
    }

    noiseFloorSamples[currentSampleIndex] = (int32_t)rssi;
    currentSampleIndex++;

    if (currentSampleIndex >= NOISE_FLOOR_SAMPLES) {
        currentSampleIndex = 0;
        isNoiseFloorBufferFull = true;
    }

    currentNoiseFloor = getAverageNoiseFloorInternal();

    LOG_TRACE("Noise floor: %d dBm (samples: %d, latest: %d dBm)", currentNoiseFloor, getNoiseFloorSampleCountInternal(), rssi);
}

uint8_t RadioLibInterface::getNoiseFloorSampleCountInternal() const
{
    return isNoiseFloorBufferFull ? NOISE_FLOOR_SAMPLES : currentSampleIndex;
}

int32_t RadioLibInterface::getAverageNoiseFloorInternal() const
{
    uint8_t sampleCount = getNoiseFloorSampleCountInternal();

    if (sampleCount == 0) {
        return NOISE_FLOOR_DEFAULT;
    }

    int32_t sum = 0;
    for (uint8_t i = 0; i < sampleCount; i++) {
        sum += noiseFloorSamples[i];
    }

    return sum / sampleCount;
}

int32_t RadioLibInterface::getAverageNoiseFloor()
{
    return getAverageNoiseFloorInternal();
}

int32_t RadioLibInterface::getNoiseFloor()
{
    return currentNoiseFloor;
}

bool RadioLibInterface::hasNoiseFloorSamples()
{
    return getNoiseFloorSampleCountInternal() > 0;
}

uint8_t RadioLibInterface::getNoiseFloorSampleCount()
{
    return getNoiseFloorSampleCountInternal();
}

void RadioLibInterface::resetNoiseFloor()
{
    currentSampleIndex = 0;
    isNoiseFloorBufferFull = false;
    currentNoiseFloor = NOISE_FLOOR_DEFAULT;
    LOG_DEBUG_RADIO("Noise floor reset - rolling window will restart");
}

bool RadioLibInterface::randomBytes(uint8_t *buffer, size_t length)
{
    if (!buffer || length == 0 || !iface) {
        return false;
    }

    // Older RadioLib versions only expose random(min, max), so fill the buffer byte-by-byte.
    for (size_t i = 0; i < length; ++i) {
        int32_t value = iface->random(0, 255);
        if (value < 0) {
            return false;
        }
        buffer[i] = static_cast<uint8_t>(value & 0xFF);
    }

    return true;
}

/** radio helper thread callback.
We never immediately transmit after any operation (either Rx or Tx). Instead we should wait a random multiple of
'slotTimes' (see definition in RadioInterface.h) taken from a contention window (CW) to lower the chance of collision.
The CW size is determined by setTransmitDelay() and depends either on the current channel utilization or SNR in case
of a flooding message. After this, we perform channel activity detection (CAD) and reset the transmit delay if it is
currently active.
*/
// In software-IRQ-poll mode (LORA_DIO1_SOFTWARE_POLL) a 1ms poll tick is almost always pending, so
// TX timers must be allowed to overwrite the pending notification or TX scheduling starves. On all
// other targets keep the historical non-overwriting behavior.
#ifdef LORA_DIO1_SOFTWARE_POLL
static constexpr bool txTimerOverwrite = true;
#else
static constexpr bool txTimerOverwrite = false;
#endif

// cppcheck-suppress constParameterPointer ; a function pointer can't meaningfully point to const
bool RadioLibInterface::isIsrTxCallback(void (*callback)())
{
    return callback == isrTxLevel0;
}

void RadioLibInterface::scheduleIrqPollTick()
{
    // Never overwrite a pending notification (especially TRANSMIT_DELAY_COMPLETED),
    // otherwise poll ticks would starve TX scheduling.
    //
    // There is a single notification slot, so while a TX is queued and the radio is busy receiving,
    // the self-rescheduling TRANSMIT_DELAY_COMPLETED timer (which does overwrite, see txTimerOverwrite)
    // can keep the slot and prevent a poll tick from being scheduled. In that window a completing
    // RX/TX is not seen by the poll; RadioInterface's pollMissedIrqs() (~1s) is the backup that
    // recovers it, so the effect is bounded added latency under heavy contention, not a lost event.
    notifyLater(1, ISR_POLL_TICK, false);
}

void RadioLibInterface::deliverPendingIrqFromPoll(PendingISR cause)
{
    disableInterrupt(); // stop polling; this is the poll-path equivalent of isrLevel0Common()
    notify(cause, true);
}

void RadioLibInterface::onNotify(uint32_t notification)
{
    // Frames the readout task captured whose ISR_RX a later notification overwrote. Deliver them, and re-arm as ISR_RX
    // would unless this thread has moved the radio on.
    if (notification != ISR_RX && deliverCapturedFrames() && isReceiving)
        notify(ISR_RX, false);

    switch (notification) {
    case ISR_TX: {
        // The chip is deaf in standby until startReceive(), so the airtime log and printPacket() wait until after it.
        meshtastic_MeshPacket *sent;
        {
            // The detach and the re-arm are one sequence: a readout between them would clear the flags the arm
            // sets up, and the hook's pre-stage switch must not land mid-readout either. The adopt below needs it
            // too: inside it the readout task cannot be partway through the re-arm it is taking over.
            RadioSequence seq(this);
            sent = handleTransmitInterrupt(); // radio already back on the home config
            // Let the hooks pre-stage the radio for the NEXT queued packet. Not required for correctness -
            // TRANSMIT_DELAY_COMPLETED asks again before the scan, which is where the answer is acted on -
            // but it keeps the post-TX listen window on the channel we are about to transmit on.
            (void)RadioTxHooks::beforeTransmit(this, txQueue.getFront());
            if (!adoptReceiveArmedFromIsr())
                startReceive();
        }
        setTransmitDelay();
        finishSentPacket(sent); // outside the lock: it only logs airtime and prints
        break;
    }
    case ISR_RX: {
        // Set false where the radio is to be left as it is. Never an early break: the task's notify(ISR_RX) may have
        // overwritten a pending TRANSMIT_DELAY_COMPLETED - one notification slot - so skipping the setTransmitDelay()
        // below would strand a queued packet until the next send() or radio interrupt happened to re-arm its timer.
        bool rearmWanted = true;
        if (rxReadoutActive()) {
            // The readout task has already taken the frames out of the radio, so this makes no RadioLib calls and
            // deliberately runs outside the radio-sequence lock: it enqueues packets, and holding the lock across
            // that would delay the next readout for no reason.
            // A CAD handoff's frame among these ends its wait (handleReceiveInterrupt() clears cadHandoffRxStart).
            deliverCapturedFrames();
            if (!isReceiving) {
                rearmWanted = false; // this thread has since moved the radio on (a scan or a TX): leave it there
            } else if (cadHandoffRxStart) {
                // No frame for the handoff. Its RX is still running, unless it expired empty: TIMEOUT alone, which the
                // task does not read, and the chip now in standby.
                RadioSequence seq(this);
                if (iface->checkIrq(RADIOLIB_IRQ_TIMEOUT) != 1)
                    rearmWanted = false; // the handoff's own RX is still running: nothing to re-arm
                else
                    handleReceiveInterrupt(); // logs it and drops the TIMEOUT; the re-arm below puts the chip back in RX
            }
        } else {
            RadioSequence seq(this);
            handleReceiveInterrupt();
        }
        if (rearmWanted) {
            // Re-arm for the next packet, as one sequence. Where the radio can tell that its RX is still running,
            // rearmReceive() picks that RX back up instead of standing by first, so a second packet that is already
            // arriving is not aborted; the others restart RX.
            RadioSequence seq(this);
            rearmReceive();
        }
        setTransmitDelay();
        break;
    }
    case ISR_POLL_TICK:
        handleSoftwareLoraIrqPoll();
        break;
    case TRANSMIT_DELAY_COMPLETED:
        if (txStageDueMs) {
            // Brought forward by scheduleTransmitDelayCompleted(): stage, then wait until the backoff really ends
            const uint32_t dueMs = txStageDueMs;
            txStageDueMs = 0;
            stageTxEarly(txQueue.getFront());
            const uint32_t now = Time::getMillis();
            if (!Throttle::deadlinePassedAt(now, dueMs)) {
                notifyLater(dueMs - now, TRANSMIT_DELAY_COMPLETED, txTimerOverwrite);
                break;
            }
        }

        // If we are not currently in receive mode, then restart the random delay (this can happen if the main thread
        // has placed the unit into standby)  FIXME, how will this work if the chipset is in sleep mode?
        if (!txQueue.empty()) {
            if (!canSendImmediately()) {
                setTransmitDelay(); // currently Rx/Tx-ing: reset random delay
            } else {
                meshtastic_MeshPacket *txp = txQueue.getFront();
                assert(txp);
                const uint32_t now = Time::getMillis();
                // Not `long remaining = tx_after - Time::getMillis()`: that uint32_t subtraction widens to
                // ~4.29e9 where long is 64-bit (portduino), rescheduling a due packet ~49.7 days out.
                if (txp->tx_after && !Throttle::deadlinePassedAt(now, txp->tx_after)) {
                    // There's still some delay pending on this packet, so resume waiting for it to elapse
                    notifyLater(txp->tx_after - now, TRANSMIT_DELAY_COMPLETED, txTimerOverwrite);
                } else if (const RadioTxHook::PreTxAction action = RadioTxHooks::beforeTransmit(this, txp);
                           action == RadioTxHook::PRETX_DROP) {
                    // A module refuses this packet on the radio config we are holding: drop it rather
                    // than transmit it, and move on to the next queued packet.
                    meshtastic_MeshPacket *bad = txQueue.dequeue();
                    LOG_DEBUG("Drop Tx packet 0x%08x, refused before transmit", bad->id);
                    RadioTxHooks::packetReleased(this, bad);
                    packetPool.release(bad);
                    setTransmitDelay();
                } else if (action == RadioTxHook::PRETX_DEFER) {
                    setTransmitDelay(); // the radio config moved, so re-run the delay and scan on it
                } else {
                    // The scan and the transmit it decides are one sequence: a readout landing between them would
                    // clear the flags startTransmit() is about to set up, or on SX128x leave the chip in standby.
                    RadioSequence seq(this);
                    // Listen-before-talk: a CAD preamble scan immediately before we key up.
                    LOG_DEBUG("CAD arm");
                    scanForTx = txp;
                    const bool channelActive = isChannelActive();
                    scanForTx = nullptr;
                    if (channelActive) { // currently traffic on the channel?
                        LOG_DEBUG("CAD busy");
                        // Beacon target or not: reconfigureForBeaconTX() already left RX running on that
                        // config, so skipping this only ever left the node deaf in standby.
                        rearmReceive();
                        setTransmitDelay();
                    } else {
                        LOG_DEBUG("CAD free");
                        // Send any outgoing packets we have ready as fast as possible to keep the time between channel scan and
                        // actual transmission as short as possible
                        txp = txQueue.dequeue();
                        assert(txp);
                        startSend(txp);
                        LOG_TRACE("%d packets in TX queue", txQueue.getMaxLen() - txQueue.getFree());
                    }
                }
            }
        } else {
            // Do nothing, because the queue is empty
        }
        break;
    default:
        assert(0); // We expected to receive a valid notification from the ISR
    }
}

void RadioLibInterface::setTransmitDelay()
{
    meshtastic_MeshPacket *p = txQueue.getFront();
    if (!p) {
        return; // noop if there's nothing in the queue
    }

    // We want all sending/receiving to be done by our daemon thread.
    // We use a delay here because this packet might have been sent in response to a packet we just received.
    // So we want to make sure the other side has had a chance to reconfigure its radio.

    if (p->tx_after) {
        unsigned long add_delay = p->rx_rssi ? getTxDelayMsecWeighted(p) : getTxDelayMsec();
        unsigned long now = Time::getMillis();
        // skipZero, not timerEndsAtMillis: this is a clamp of three candidates rather than a plain
        // now + delay, and `if (p->tx_after)` above is the read that takes 0 as "no delay wanted" -
        // so a recomputation landing on 0 drops the CSMA backoff and the packet goes out at once.
        //
        // Narrow to uint32_t BEFORE skipZero, not after: add_delay is unsigned long, 64-bit on the
        // portduino host, so the clamp can exceed UINT32_MAX there. skipZero on the wide value would
        // pass 0x100000000 through as non-zero and the store to this uint32_t field would truncate it
        // back to the 0 being avoided.
        p->tx_after = Time::skipZero(
            (uint32_t)min(max(p->tx_after + add_delay, now + add_delay), now + 2 * getTxDelayMsecWeightedWorst(p->rx_snr)));
        scheduleTransmitDelayCompleted(p->tx_after - now);
    } else if (p->rx_snr == 0 && p->rx_rssi == 0) {
        /* We assume if rx_snr = 0 and rx_rssi = 0, the packet was generated locally.
         *   This assumption is valid because of the offset generated by the radio to account for the noise
         *   floor.
         */
        startTransmitTimer(true);
    } else {
        // If there is a SNR, start a timer scaled based on that SNR.
        LOG_TRACE("rx_snr found. hop_limit:%d rx_snr:%f", p->hop_limit, p->rx_snr);
        startTransmitTimerRebroadcast(p);
    }
}

void RadioLibInterface::startTransmitTimer(bool withDelay)
{
    // If we have work to do and the timer wasn't already scheduled, schedule it now
    if (!txQueue.empty()) {
        uint32_t delay = !withDelay ? 1 : getTxDelayMsec();
        scheduleTransmitDelayCompleted(delay); // This will implicitly enable
    }
}

void RadioLibInterface::startTransmitTimerRebroadcast(meshtastic_MeshPacket *p)
{
    // If we have work to do and the timer wasn't already scheduled, schedule it now
    if (!txQueue.empty()) {
        uint32_t delay = getTxDelayMsecWeighted(p);
        scheduleTransmitDelayCompleted(delay); // This will implicitly enable
    }
}

void RadioLibInterface::scheduleTransmitDelayCompleted(uint32_t delay)
{
    // Where the driver wants it, fire at once so the radio thread writes the payload while RX runs, then wait out the
    // rest of the backoff
    txStageDueMs = 0;
    if (delay >= TX_STAGE_EARLY_MIN_MS && wantsEarlyTxStage()) {
        txStageDueMs = Time::timerEndsAtMillis(delay);
        delay = 1;
    }
    notifyLater(delay, TRANSMIT_DELAY_COMPLETED, txTimerOverwrite);
}

/**
 * If the packet is not already in the late rebroadcast window, move it there
 */
void RadioLibInterface::clampToLateRebroadcastWindow(NodeNum from, PacketId id)
{
    // Look for non-late packets only, so we don't do this twice!
    meshtastic_MeshPacket *p = txQueue.remove(from, id, true, false);
    if (p) {
        p->tx_after = Time::timerEndsAtMillis(getTxDelayMsecWeightedWorst(p->rx_snr));
        bool dropped = false;
        if (txQueue.enqueue(p, &dropped)) {
            LOG_TRACE("Move queued packet to late rebroadcast window %ums from now", (uint32_t)(p->tx_after - millis()));
        } else {
            packetPool.release(p);
        }
        if (dropped) {
            txDrop++;
        }
    }
}

/**
 * If there is a packet pending TX in the queue with a worse hop limit, remove it pending replacement with a better version
 * @return Whether a pending packet was removed
 */
bool RadioLibInterface::removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt)
{
    meshtastic_MeshPacket *p = txQueue.remove(from, id, true, true, hop_limit_lt);
    if (p) {
        LOG_DEBUG_RADIO("Drop pending-TX packet 0x%08x, hop limit %d", p->id, p->hop_limit);
        RadioTxHooks::packetReleased(this, p);
        packetPool.release(p);
        return true;
    }
    return false;
}

meshtastic_MeshPacket *RadioLibInterface::handleTransmitInterrupt()
{
    // Null if we forced the device into standby, which already completed the send.
    meshtastic_MeshPacket *sent = detachSentPacket();
    powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn); // But our transmitter is definitely off now
    return sent;
}

void RadioLibInterface::completeSending()
{
    finishSentPacket(detachSentPacket());
}

meshtastic_MeshPacket *RadioLibInterface::detachSentPacket()
{
    // Cleared first: printPacket() in finishSentPacket() can take a long time.
    auto p = sendingPacket;
    sendingPacket = NULL;
#ifdef LED_LORA
    digitalWrite(LED_LORA, LED_STATE_OFF);
#endif
    // Keep this behind `if (p)`: completeSending() also runs on every setStandby(), where a hook
    // undoing its own pre-TX switch would recurse back through reconfigure().
    if (p)
        RadioTxHooks::packetReleased(this, p);
    return p;
}

void RadioLibInterface::finishSentPacket(meshtastic_MeshPacket *p)
{
    if (!p)
        return;
    // Packet has been sent, count it toward our TX airtime utilization.
    uint32_t xmitMsec = getPacketTime(p);
    airTime->logAirtime(TX_LOG, xmitMsec);

    txGood++;
    if (!isFromUs(p))
        txRelay++;
    printPacket("Completed sending", p);

    // We are done sending that packet, release it
    packetPool.release(p);
}

/// Whether readData() came back with the chip's terminal flags still latched. Every driver decides a CRC or damaged-header
/// verdict BEFORE its clear and returns it after (SX126x asserts crcState one line past clearIrqStatus(); SX127x has no
/// early return at all), so those two codes mean the flags are already gone and clearing again could take the RX_DONE of a
/// frame that arrived meanwhile. Every other error - an Rx timeout, a failed SPI stream check or buffer read, a wrong
/// modem - returns before the clear, leaving the flags to be dropped by hand.
static bool readDataLeftIrqFlags(int16_t state)
{
    return state != RADIOLIB_ERR_NONE && state != RADIOLIB_ERR_CRC_MISMATCH && state != RADIOLIB_ERR_LORA_HEADER_DAMAGED;
}

void RadioLibInterface::handleReceiveInterrupt(const CapturedRxInfo *captured)
{
    const bool wasCadHandoff = cadHandoffRxStart != 0;
    cadHandoffRxStart = 0; // this RX ends the wait either way; the outcome is logged below
    preambleHoldStart = 0; // likewise the reception a held preamble announced

    size_t length;
    if (captured) {
        length = captured->len; // the readout task already took the frame into radioBuffer
    } else {
        // when this is called, we should be in receive mode - if we are not, just jump out instead of bombing. Possible
        // Race Condition?
        if (!isReceiving) {
            LOG_ERROR("handleReceiveInterrupt called while not in rx mode");
            clearReadIrqs(); // nothing will read this frame out, and a latched RX_DONE holds DIO1 high
            return;
        }

        isReceiving = false;

        // A CAD handoff's RX window expired with nothing on air. There is no packet to read, so don't count
        // it as a bad one - the caller's rearmReceive() puts the radio back to listening.
        if (iface->checkIrq(RADIOLIB_IRQ_RX_DONE) != 1 && iface->checkIrq(RADIOLIB_IRQ_TIMEOUT) == 1) {
            LOG_DEBUG("CAD>RX empty");
            iface->clearIrq(1UL << RADIOLIB_IRQ_TIMEOUT);
            return;
        }

        // read the number of actually received bytes
        length = iface->getPacketLength();
    }

    if (wasCadHandoff)
        LOG_DEBUG("CAD>RX pkt");

    // Some drivers report this as a 16 bit value, so a bad readback can overrun radioBuffer in readData()
    if (length > sizeof(radioBuffer)) {
        LOG_ERROR("Ignore rx packet, bad length %u", (unsigned int)length);
        rxBad++;
        if (!captured) // for a captured frame the readout task's readData() already cleared them
            clearReadIrqs();
        return;
    }

    rxCapturedHeader = captured; // null on the thread path, where the chip still holds this frame's status
    uint32_t rxMsec = getPacketTime(length, true);
    rxCapturedHeader = nullptr;

#ifndef DISABLE_WELCOME_UNSET
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        LOG_WARN("lora rx disabled: Region unset");
        airTime->logAirtime(RX_ALL_LOG, rxMsec);
        if (!captured) // for a captured frame the readout task's readData() already cleared them
            clearReadIrqs();
        return;
    }
#endif

    int state = captured ? captured->state : iface->readData((uint8_t *)&radioBuffer, length);
#if ARCH_PORTDUINO
    if (portduino_config.logoutputlevel == level_trace) {
        printBytes("Raw incoming packet: ", (uint8_t *)&radioBuffer, length);
    }
#endif
    if (rxFrameOverlapsTxStage(length) && state == RADIOLIB_ERR_NONE) { // called first: it tracks every readout
        // The chip's CRC covered what came over the air, not the buffer our payload was written into
        LOG_WARN("Drop rx packet, %u bytes: a TX payload was staged over its buffer while it arrived", (unsigned)length);
        rxBad++;
        airTime->logAirtime(RX_ALL_LOG, rxMsec);
        return;
    }
    if (state != RADIOLIB_ERR_NONE) {
        // Log PacketHeader similar to RadioInterface::printPacket so we can try to match RX errors to other packets in the logs.
        LOG_ERROR("Ignore rx packet, error=%d (maybe id=0x%08x fr=0x%08x to=0x%08x flags=0x%02x rxSNR=%g rxRSSI=%i "
                  "nextHop=0x%x relay=0x%x)",
                  state, radioBuffer.header.id, radioBuffer.header.from, radioBuffer.header.to, radioBuffer.header.flags,
                  captured ? captured->snr : iface->getSNR(), captured ? (long)captured->rssi : lround(iface->getRSSI()),
                  radioBuffer.header.next_hop, radioBuffer.header.relay_node);
        rxBad++;

        airTime->logAirtime(RX_ALL_LOG, rxMsec);
        // Only where readData() returned before its own clear. A CRC mismatch is the common error on a noisy
        // channel and comes back with the flags already cleared, so clearing again would drop the RX_DONE of a
        // frame that completed while this error was being logged. The task clears its own failed reads, so a
        // captured frame is never ours to clear.
        if (!captured && readDataLeftIrqFlags(state))
            clearReadIrqs();

    } else {
        // Skip the 4 headers that are at the beginning of the rxBuf
        int32_t payloadLen = length - sizeof(PacketHeader);

        // check for short packets
        if (payloadLen < 0) {
            LOG_WARN("Ignore received packet too short");
            rxBad++;
            airTime->logAirtime(RX_ALL_LOG, rxMsec);
        } else {
            rxGood++;
            lastRxGoodMs = millis();
            // altered packet with "from == 0" can do Remote Node Administration without permission
            if (radioBuffer.header.from == 0) {
                LOG_WARN("Ignore received packet without sender");
                return;
            }

            // Note: we deliver _all_ packets to our router (i.e. our interface is intentionally promiscuous).
            // This allows the router and other apps on our node to sniff packets (usually routing) between other
            // nodes.
            meshtastic_MeshPacket *mp = packetPool.allocZeroed();
            if (!mp) {
                airTime->logAirtime(RX_LOG, rxMsec);
                return;
            }

            // Keep the assigned fields in sync with src/mqtt/MQTT.cpp:onReceiveProto
            mp->from = radioBuffer.header.from;
            mp->to = radioBuffer.header.to;
            mp->id = radioBuffer.header.id;
            mp->channel = radioBuffer.header.channel;
            assert(HOP_MAX <= PACKET_FLAGS_HOP_LIMIT_MASK); // If hopmax changes, carefully check this code
            mp->hop_limit = radioBuffer.header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
            mp->hop_start = (radioBuffer.header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
            mp->want_ack = !!(radioBuffer.header.flags & PACKET_FLAGS_WANT_ACK_MASK);
            mp->via_mqtt = !!(radioBuffer.header.flags & PACKET_FLAGS_VIA_MQTT_MASK);
            // If hop_start is not set, next_hop and relay_node are invalid (firmware <2.3)
            mp->next_hop = mp->hop_start == 0 ? NO_NEXT_HOP_PREFERENCE : radioBuffer.header.next_hop;
            mp->relay_node = mp->hop_start == 0 ? NO_RELAY_NODE : radioBuffer.header.relay_node;

            if (captured) {
                // What addReceiveMetadata() reads, read by the task before the next frame could replace it
                mp->rx_snr = captured->snr;
                mp->rx_rssi = captured->rssi;
                mp->has_rx_rssi = true;
            } else {
                addReceiveMetadata(mp);
            }

            mp->which_payload_variant =
                meshtastic_MeshPacket_encrypted_tag; // Mark that the payload is still encrypted at this point
            assert(((uint32_t)payloadLen) <= sizeof(mp->encrypted.bytes));
            memcpy(mp->encrypted.bytes, radioBuffer.payload, payloadLen);
            mp->encrypted.size = payloadLen;

            printPacket("Lora RX", mp);

#ifdef LED_LORA
            loraRxPacketObservable.notifyObservers(mp->from);
#endif

            airTime->logAirtime(RX_LOG, rxMsec);

            deliverToReceiver(mp);
        }
    }
}

void RadioLibInterface::startReceive()
{
#ifdef MESHTASTIC_RX_READOUT_TASK
    if (!rxReadoutTaskTried)
        startRxReadoutTask();
#endif
    isReceiving = true;
    // Drivers only reach here once the chip actually accepted the RX start, so the radio is alive again.
    // This is the sole place the recovery ladder is cleared - nothing short of an armed RX counts as fixed.
    rxOffline = false;
    chipRecoveryFailures = 0;
    rxFlagsSeenMs = 0;
    rxFlagsSeenHeader = false;
    powerMon->setState(meshtastic_PowerMon_State_Lora_RXOn);
}

void RadioLibInterface::pollMissedIrqs()
{
    // RadioLibInterface::enableInterrupt uses EDGE-TRIGGERED interrupts. Poll as a backup to catch missed edges.
    // The main loop's flag reads are a sequence too: an LR11x0/LR20x0 status read zeroes RadioLib's per-Module SPI widths
    // for its transfer, so a readout landing inside one would take the chip's status bytes for the frame length.
    RadioSequence seq(this);
    if (isReceiving) {
        checkRxDoneIrqFlag();
        checkCadHandoffTimeout();
        checkStaleRxFlags();
    }
    if (sendingPacket) {
        checkTxDoneIrqFlag();
    }
}

bool RadioLibInterface::resetAGC()
{
    // Base implementation: no-op. Override in chip-specific subclasses.
    return false;
}

void RadioLibInterface::noteCadHandoffToRx()
{
    LOG_DEBUG("CAD>RX started");
    cadHandedToRx = true;
    // Same clock Throttle compares against, so a native test can drive both across the wrap. 0 is the
    // "none outstanding" sentinel and getMillis() does land on it once per wrap, so step past it.
    const uint32_t now = Time::getMillis();
    cadHandoffRxStart = now ? now : 1;
}

bool RadioLibInterface::irqPolledOverUsb() const
{
#ifdef ARCH_PORTDUINO
    return portduino_config.lora_spi_dev == "ch341";
#else
    return false;
#endif
}

void RadioLibInterface::rearmReceive()
{
    // The flag is spent here, so every later call takes the full path - including RX_DONE after a
    // handoff, whose bounded RX has already dropped the chip to standby.
    if (!cadHandedToRx) {
        if (!resumeRunningReceive())
            startReceive();
        return;
    }
    cadHandedToRx = false;
    // Same order the drivers' own startReceive() uses: mark receiving BEFORE arming, or an ISR that
    // fires in between reaches handleReceiveInterrupt() while isReceiving is still false and is dropped.
    RadioLibInterface::startReceive();
    enableInterrupt(isrRxLevel0);
    // The line is not known-low here, and the ISR is rising-edge: catch an RX_DONE that beat the arm.
    checkRxDoneIrqFlag();
}

void RadioLibInterface::checkCadHandoffTimeout()
{
    // Backstop to the chip's own cadTimeout, which is the primary bound. Doubled so the hardware always
    // expires first; this only catches an RX whose timer stopped on a header that never completed.
    const uint32_t maxPacketTimeMsec = getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader));
    if (cadHandoffRxStart && Throttle::hasElapsed(cadHandoffRxStart, 2 * maxPacketTimeMsec)) {
        LOG_WARN("CAD>RX timeout");
        cadHandoffRxStart = 0;
        // Reached from the main loop, which holds no sequence of its own: a readout landing inside this
        // re-arm would clear the flags it sets up, exactly as it would on the radio thread's paths.
        RadioSequence seq(this);
        startReceive();
    }
}

void RadioLibInterface::checkStaleRxFlags()
{
    // A handoff RX has its own timeout, and a pending RX_DONE is about to clear every flag itself.
    if (cadHandoffRxStart)
        return;
    // The flag read, the verdict drawn from it and the re-arm or clear that follows are one sequence: a readout
    // in between retires the very flags being judged, so the verdict would be drawn on flags that are gone.
    RadioSequence seq(this);
    const uint32_t irq = iface->getIrqFlags();
    if (irq & iface->getIrqMapped(1UL << RADIOLIB_IRQ_RX_DONE))
        return;
    // HEADER_ERR counts: on SX1280 it leaves RX wedged with no RX_DONE or TIMEOUT to follow.
    const bool headerSeen = irq & iface->getIrqMapped((1UL << RADIOLIB_IRQ_HEADER_VALID) | (1UL << RADIOLIB_IRQ_HEADER_ERR));
    const bool preambleSeen = irq & iface->getIrqMapped(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    if (!headerSeen && !preambleSeen) {
        rxFlagsSeenMs = 0;
        rxFlagsSeenHeader = false;
        return;
    }
    // A header's window starts when the header shows, not when an earlier bare preamble did
    if (!rxFlagsSeenMs || (headerSeen && !rxFlagsSeenHeader)) {
        rxFlagsSeenMs = Time::skipZero(Time::getMillis());
        rxFlagsSeenHeader = headerSeen;
        return;
    }

    const uint32_t maxPacketTimeMsec = getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader));
    switch (staleRxFlagAction(headerSeen, Time::getMillis() - rxFlagsSeenMs, maxPacketTimeMsec)) {
    case StaleRxFlagAction::Keep:
        break;
    case StaleRxFlagAction::Rearm:
        LOG_DEBUG("RX header stale, re-arm");
        startReceive(); // clears rxFlagsSeenMs
        break;
    case StaleRxFlagAction::ClearPreamble:
        // A clear never aborts a reception, unlike the standby inside startReceive().
        LOG_DEBUG("RX preamble stale, cleared");
        iface->clearIrq(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
        rxFlagsSeenMs = 0;
        rxFlagsSeenHeader = false;
        break;
    }
}

void RadioLibInterface::periodicRadioMaintenance()
{
    // Every startReceive() call site is event-driven (RX/TX ISR, the CAD-busy branch, reconfigure), and a
    // radio left with RX off can no longer raise an RX interrupt - on a node with nothing to transmit
    // nothing would ever re-arm it. This periodic tick is that retry; maybeRecoverChipStateLoss() throttles.
    if (rxOffline) {
        LOG_WARN("Radio RX offline, retrying");
        RadioSequence seq(this); // a re-init and the arm that follows it are one sequence
        if (maybeRecoverChipStateLoss())
            startReceive();
        return; // a chip just re-inited (or still dead) has no use for an AGC reset this tick
    }
    // resetAGC() ends in startReceive(), whose standby would run a queued packet's TX delay, start it and cut it off.
    if (hasQueuedTx())
        return;

    // A radio that is still decoding packets has gain that isn't stuck
    const bool hearing = lastRxGoodMs && Throttle::isWithinTimespanMs(lastRxGoodMs, AGC_IDLE_RESET_MS);
    if (hearing && lastAgcResetMs && Throttle::isWithinTimespanMs(lastAgcResetMs, AGC_FORCED_RESET_MS))
        return;
    // resetAGC() is a sequence end to end - warm sleep, RC standby, CALIBRATE_ALL, the re-applied settings and
    // the startReceive() that resumes - and the chip is not readable for any of it. Its own mid-packet bail is a
    // point check, so it cannot exclude a readout that starts just after it: the lock is what does.
    bool agcReset = false;
    {
        RadioSequence seq(this);
        agcReset = resetAGC();
    }
    if (agcReset) {
        const uint32_t now = millis();
        lastAgcResetMs = now ? now : 1;
    }
}

bool RadioLibInterface::maybeRecoverChipStateLoss()
{
    // One attempt per window: the transient resets this recovers from need a single re-init, and a
    // chip that stays dead must not stall the TX/RX paths with a begin() attempt on every call
    if (lastChipRecoveryMs && Throttle::isWithinTimespanMs(lastChipRecoveryMs, 30 * 1000UL)) {
        LOG_DEBUG("Radio recovery suppressed, %us since the last attempt", (Time::getMillis() - lastChipRecoveryMs) / 1000);
        return false;
    }

    // The ladder counts re-arms, not re-inits: only RadioLibInterface::startReceive() clears the count, and
    // only once the chip really accepted RX. Judging the previous attempt here - a throttle window later,
    // after its retry - is what stops a begin() that succeeded while leaving RX dead from crediting itself.
    if (chipRecoveryFailures >= MAX_CHIP_RECOVERY_FAILURES && rebootAtMsec == 0) {
        // Attempts are a throttle window apart, so this is minutes of a provably deaf chip. begin() alone
        // clearly isn't reviving it; reboot to re-run init(), which redoes the power-on sequence it skips.
        LOG_ERROR("Radio still deaf after %u re-inits, rebooting", chipRecoveryFailures);
        rebootAtMsec = Time::timerEndsAtMillis(DEFAULT_REBOOT_SECONDS * 1000);
    }
    chipRecoveryFailures++;

    lastChipRecoveryMs = Time::skipZero(Time::getMillis());
    RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
    LOG_ERROR("Radio chip state lost mid-operation, re-init");
    RadioSequence seq(this); // begin() and the chip setup behind it must not be split by a readout
    bool recovered = recoverChipStateLoss();
    LOG_INFO("Radio re-init %s", recovered ? "succeeded" : "failed");
    return recovered;
}

void RadioLibInterface::clearReadIrqs()
{
    iface->clearIrq((1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_CRC_ERR));
    discardUnreadRxFrame();
}

bool RadioLibInterface::checkRxDoneIrqFlag()
{
    if (iface->checkIrq(RADIOLIB_IRQ_RX_DONE)) {
        if (wakeRxReadout()) { // the readout task takes it, and notifies ISR_RX itself
            LOG_WARN("caught missed RX_DONE, woke the readout task");
            return true;
        }
        LOG_WARN("caught missed RX_DONE");
        notify(ISR_RX, true);
        return true;
    }
    return false;
}

#ifdef MESHTASTIC_RX_READOUT_TASK
// ESP-IDF counts a task's stack in bytes, the other ports in words
#ifdef ARCH_ESP32
#define RX_READOUT_STACK 4096
#else
#define RX_READOUT_STACK 512
#endif

bool RadioLibInterface::lockRadioSequence()
{
    // Null before the task exists, and there is nothing to exclude until then
    if (!radioSeqMutex)
        return false;
    return xSemaphoreTakeRecursive(radioSeqMutex, portMAX_DELAY) == pdTRUE;
}

void RadioLibInterface::unlockRadioSequence()
{
    if (radioSeqMutex)
        xSemaphoreGiveRecursive(radioSeqMutex);
}

void RadioLibInterface::startRxReadoutTask()
{
    rxReadoutTaskTried = true;
    // Before the task, so it can never run unguarded
    radioSeqMutex = xSemaphoreCreateRecursiveMutex();
    if (!radioSeqMutex) {
        LOG_ERROR("RX readout task not started, no mutex");
        return;
    }
    // Above the main loop, so a long hold there cannot delay a readout
    UBaseType_t priority = uxTaskPriorityGet(nullptr) + 2;
    if (priority < tskIDLE_PRIORITY + 3)
        priority = tskIDLE_PRIORITY + 3;
    if (priority > configMAX_PRIORITIES - 1)
        priority = configMAX_PRIORITIES - 1;
    // On the main loop's core, so the task preempts the loop as it would on one core: none of the loop's RadioLib calls
    // can fall between the task's.
    TaskHandle_t task = nullptr;
    int core = -1;
#ifdef ARCH_ESP32
    core = xPortGetCoreID();
    if (xTaskCreatePinnedToCore(rxReadoutTaskMain, "RxReadout", RX_READOUT_STACK, this, priority, &task, core) != pdPASS)
        task = nullptr;
#else
    if (xTaskCreate(rxReadoutTaskMain, "RxReadout", RX_READOUT_STACK, this, priority, &task) != pdPASS)
        task = nullptr;
#if defined(ARCH_RP2040) && configUSE_CORE_AFFINITY == 1 && configNUMBER_OF_CORES > 1
    if (task) {
        core = rp2040.cpuid();
        vTaskCoreAffinitySet(task, 1u << core);
    }
#endif
#endif
    rxReadoutTask = task;
    LOG_INFO("RX readout task %s, priority %u (loop %u), core %d", task ? "started" : "not started", (unsigned)priority,
             (unsigned)uxTaskPriorityGet(nullptr), core);
}

/// RX_DONE: wake the readout task and return. The radio is read there, with interrupts enabled, not here.
bool INTERRUPT_ATTR RadioLibInterface::rxDoneFromIsr()
{
    if (!rxReadoutTask)
        return false;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(rxReadoutTask, &woken);
    YIELD_FROM_ISR(woken);
    return true;
}

bool RadioLibInterface::wakeRxReadout()
{
    if (!rxReadoutTask)
        return false;
    // No wait here. This is called from inside driver RX arms, which hold the radio-sequence lock, so waiting for a
    // task that needs that same lock would deadlock. The lock is also what makes waiting unnecessary: the task
    // cannot touch the radio until the caller's sequence ends, and it notifies ISR_RX itself once it has.
    xTaskNotifyGive(rxReadoutTask);
    return true;
}

bool INTERRUPT_ATTR RadioLibInterface::requestRearmFromIsr()
{
    if (!rxReadoutTask)
        return false;
    rxRearmFromTaskPending = true;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(rxReadoutTask, &woken);
    YIELD_FROM_ISR(woken);
    return true;
}

void RadioLibInterface::rxReadoutTaskMain(void *arg)
{
    auto *self = static_cast<RadioLibInterface *>(arg);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (self->rxRearmFromTaskPending) {
            self->rxRearmFromTaskPending = false;
            self->rearmReceiveFromTask();
        }
        self->readOutFromTask();
    }
}

/// The RadioLib calls handleReceiveInterrupt() and addReceiveMetadata() make, as soon as RX_DONE rises. Each takes the SPI
/// lock for itself; the radio-sequence lock below is what keeps the whole readout out of the radio thread's sequences.
void RadioLibInterface::readOutFromTask()
{
    // Never between the RadioLib calls of a sequence on the radio thread: readData() below clears the chip's IRQ
    // flags, and on SX128x drops it to standby, either of which would break a scan, arm or transmit in flight.
    RadioSequence seq(this);
    if (iface->checkIrq(RADIOLIB_IRQ_RX_DONE) != 1) {
        // A CAD handoff's RX that expired empty raises TIMEOUT alone. The radio thread logs it and re-arms; left here, the
        // chip would stay in standby until checkCadHandoffTimeout().
        if (cadHandoffRxStart && iface->checkIrq(RADIOLIB_IRQ_TIMEOUT) == 1)
            notify(ISR_RX, !rxArmedBeforeTxDone);
        // A handoff's RX also routes CRC_ERR and HEADER_ERR to DIO1, and a damaged header raises one of those with no
        // RX_DONE behind it. Left latched, the line stays asserted and the next frame's RX_DONE raises no edge:
        // checkStaleRxFlags() steps aside for a handoff, so nothing would notice until checkCadHandoffTimeout().
        // Only inside the handoff window: plain RX leaves HEADER_ERR for checkStaleRxFlags() to judge a wedged SX1280 on.
        if (cadHandoffRxStart) {
            const uint32_t errIrqs = (1UL << RADIOLIB_IRQ_CRC_ERR) | (1UL << RADIOLIB_IRQ_HEADER_ERR);
            if (iface->getIrqFlags() & iface->getIrqMapped(errIrqs))
                iface->clearIrq(errIrqs);
        }
        return; // otherwise an edge for a frame already taken
    }
    const size_t len = iface->getPacketLength();
    const uint8_t head = rxRingHead;
    const uint8_t next = (uint8_t)((head + 1) % rxRingSize);
    if (len > sizeof(rxRing[0].data) || next == rxRingTail) {
        // A length that would overrun the buffer, or the thread has not taken the last 8: the frame is lost. Clear its
        // flags so the next RX_DONE raises a fresh edge.
        clearReadIrqs();
        if (len > sizeof(rxRing[0].data))
            rxReadoutBadLength = rxReadoutBadLength + 1;
        else
            rxReadoutDropped = rxReadoutDropped + 1;
        notify(ISR_RX, !rxArmedBeforeTxDone); // for the counter line, and the re-arm
        return;
    }
    CapturedFrame &f = rxRing[head];
    f.info.state = iface->readData(f.data, len);
    if (readDataLeftIrqFlags(f.info.state)) {
        // This read came back before RadioLib's own clear, so RX_DONE is still latched for a frame nobody will ever
        // read. Drop it here, while we know that: leaving it would have checkRxDoneIrqFlag() wake this task again for
        // the same dead event, round after round. The radio thread cannot tell - a captured frame's flags are not its
        // to judge - which is why this belongs to the reader.
        clearReadIrqs();
    }
    f.info.snr = iface->getSNR();
    f.info.rssi = lround(iface->getRSSI());
    f.info.len = (uint16_t)len;
    // Here, not where the radio thread accounts for the frame: by then the chip has been put back in RX - after a
    // resume it never left it - and its packet status describes the frame it is receiving now, not this one.
    f.info.headerInfoValid = readRxHeaderInfo(f.info.rxCR, f.info.hasCRC);
    rxReadoutFrames = rxReadoutFrames + 1;
    __asm__ __volatile__("" ::: "memory"); // the entry is written before the head that publishes it
    rxRingHead = next;
    notify(ISR_RX, !rxArmedBeforeTxDone);
}

bool RadioLibInterface::takeCapturedFrame(CapturedRxInfo &info)
{
    // The task's counters, logged from this thread whenever a frame was lost
    static uint32_t loggedDropped = 0, loggedBadLength = 0;
    if (rxReadoutDropped != loggedDropped || rxReadoutBadLength != loggedBadLength) {
        loggedDropped = rxReadoutDropped;
        loggedBadLength = rxReadoutBadLength;
        LOG_WARN("RX readout task: %u frames read, %u dropped (ring full), %u bad length", (unsigned)rxReadoutFrames,
                 (unsigned)loggedDropped, (unsigned)loggedBadLength);
    }
    if (rxRingTail == rxRingHead)
        return false;
    __asm__ __volatile__("" ::: "memory"); // read the entry only after seeing the head that published it
    const CapturedFrame &f = rxRing[rxRingTail];
    info = f.info;
    memcpy(&radioBuffer, f.data, info.len);
    __asm__ __volatile__("" ::: "memory"); // and free its slot only after reading it
    rxRingTail = (uint8_t)((rxRingTail + 1) % rxRingSize);
    return true;
}

unsigned RadioLibInterface::deliverCapturedFrames()
{
    unsigned delivered = 0;
    CapturedRxInfo info;
    while (takeCapturedFrame(info)) {
        delivered++;
        handleReceiveInterrupt(&info);
    }
    return delivered;
}
#endif

void RadioLibInterface::checkTxDoneIrqFlag()
{
    if (iface->checkIrq(RADIOLIB_IRQ_TX_DONE)) {
        LOG_WARN("caught missed TX_DONE");
        notify(ISR_TX, true);
    }
}

void RadioLibInterface::configHardwareForSend()
{
    powerMon->setState(meshtastic_PowerMon_State_Lora_TXOn);
}

int16_t RadioLibInterface::launchTransmit(size_t numbytes)
{
    return iface->startTransmit((uint8_t *)&radioBuffer, numbytes);
}

void RadioLibInterface::setStandby()
{
    // Any handoff is void once the chip leaves RX. Left set, the flag would make the next rearmReceive()
    // a no-op on a standby chip - deaf with no recovery - and the window would re-arm over a live packet.
    if (cadHandedToRx) // standby between the handoff and the re-arm that adopts it: should not happen
        LOG_WARN("CAD>RX void");
    cadHandedToRx = false;
    cadHandoffRxStart = 0;

    // neither sending nor receiving
    powerMon->clearState(meshtastic_PowerMon_State_Lora_RXOn);
    powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn);
}

/** start an immediate transmit */
bool RadioLibInterface::startSend(meshtastic_MeshPacket *txp)
{
    cadHandoffRxStart = 0; // TX ends any handoff wait; completeSending() re-arms RX itself

    /* NOTE: Minimize the actions before startTransmit() to keep the time between
             channel scan and actual transmit as low as possible to avoid collisions. */
    if (disabled || !config.lora.tx_enabled) {
        LOG_WARN("Drop Tx packet: LoRa Tx disabled");
        // Never reaches completeSending(), so any per-packet radio state has to be released here.
        RadioTxHooks::packetReleased(this, txp);
        packetPool.release(txp);
        // We got here through isChannelActive(), which left the chip in standby for a transmit that is
        // no longer happening. Without this the node stays deaf until something else re-arms it.
        startReceive();
        return false;
    } else {
        configHardwareForSend(); // must be after setStandby

        size_t numbytes = beginSending(txp);

        int res = launchTransmit(numbytes);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("startTransmit failed, error=%d", res);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_RADIO_SPI_BUG);

            // This send failed, but make sure to 'complete' it properly
            completeSending();
            powerMon->clearState(meshtastic_PowerMon_State_Lora_TXOn); // Transmitter off now
            startReceive(); // Restart receive mode (because startTransmit failed to put us in xmit mode)
        } else {
            // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register
            // bits
            enableInterrupt(isrTxLevel0);
            // unset-sentinel-ok: busyTx/sendingPacket is the armed flag, so 0 is a legal stamp
            lastTxStart = Time::getMillis();
            printPacket("Started Tx", txp);
#ifdef LED_LORA
            digitalWrite(LED_LORA, LED_STATE_ON);
#endif
        }

        return res == RADIOLIB_ERR_NONE;
    }
}
