#pragma once

#include "MeshPacketQueue.h"
#include "RadioInterface.h"
#include "concurrency/NotifiedWorkerThread.h"

#include <RadioLib.h>
#include <sys/types.h>

// ESP32 has special rules about ISR code
#ifdef ARDUINO_ARCH_ESP32
#define INTERRUPT_ATTR IRAM_ATTR
#else
#define INTERRUPT_ATTR
#endif

#define RADIOLIB_PIN_TYPE uint32_t

// Each received frame is read out of the radio by a FreeRTOS task woken by RX_DONE, rather than whenever the main loop next
// runs the radio thread: the radio holds one frame, and the next one overwrites it. It needs FreeRTOS and a DIO1 interrupt;
// -DMESHTASTIC_EXCLUDE_READOUT_TASK=1 keeps the readout on the radio thread.
#if defined(HAS_FREE_RTOS) && !defined(ARCH_PORTDUINO) && !defined(LORA_DIO1_SOFTWARE_POLL) && !MESHTASTIC_EXCLUDE_READOUT_TASK
#define MESHTASTIC_RX_READOUT_TASK
#endif

// In addition to the default Rx flags, we need the PREAMBLE_DETECTED flag to detect whether we are actively receiving
#define MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED))

#define AGC_RESET_INTERVAL_MS (60 * 1000) // 60 seconds: how often the loop runs periodicRadioMaintenance()
// An AGC reset takes the radio off the air for over 100 ms, so it runs only on a radio that has decoded nothing for
// AGC_IDLE_RESET_MS (gain stuck low would look like that), or at least once per AGC_FORCED_RESET_MS on a busy one
#ifndef AGC_IDLE_RESET_MS
#define AGC_IDLE_RESET_MS (60 * 1000UL)
#endif
#ifndef AGC_FORCED_RESET_MS
#define AGC_FORCED_RESET_MS (24 * 60 * 60 * 1000UL)
#endif

/**
 * We need to override the RadioLib ArduinoHal class to add mutex protection for SPI bus access
 */
class LockingArduinoHal : public ArduinoHal
{
  public:
    LockingArduinoHal(SPIClass &spi, SPISettings spiSettings) : ArduinoHal(spi, spiSettings) {};

    void spiBeginTransaction() override;
    void spiEndTransaction() override;
#if ARCH_PORTDUINO
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;

#endif
};

// TCXO_OPTIONAL (variant define) or Lora.TCXO_OPTIONAL (Portduino YAML): probe for a TCXO and
// fall back to the XTAL. LR11x0 tries XTAL first - TCXO-first hangs RadioLib's calibration wait.
#if ARCH_PORTDUINO
#define TCXO_OPTIONAL_ENABLED (portduino_config.tcxo_optional)
#elif defined(TCXO_OPTIONAL)
#define TCXO_OPTIONAL_ENABLED true
#else
#define TCXO_OPTIONAL_ENABLED false
#endif

// RadioLib's own default Vref, for a probe with no explicit voltage configured.
#define TCXO_OPTIONAL_DEFAULT_VOLTAGE 1.6f

// TCXO start-up delay programmed after every begin(), in place of RadioLib's 5000 us. A clear CAD leaves the chip on
// its RC oscillator, so the TX after it waits this long before the PA ramps. Set it in variant.h for a slower TCXO.
#ifndef TCXO_STARTUP_DELAY_US
#define TCXO_STARTUP_DELAY_US 1000
#endif

// How long RadioLib waits on BUSY before failing a command; its own default is 1000 ms. A chip whose command was split
// holds BUSY until the wait gives up, and the radio is deaf meanwhile. 0 keeps RadioLib's default.
#ifndef MESHTASTIC_RADIOLIB_SPI_TIMEOUT_MS
#define MESHTASTIC_RADIOLIB_SPI_TIMEOUT_MS 50
#endif

#if defined(USE_STM32WLx)
/**
 * A wrapper for the RadioLib STM32WLx_Module class, that doesn't connect any pins as they are virtual
 */
class STM32WLx_ModuleWrapper : public STM32WLx_Module
{
  public:
    STM32WLx_ModuleWrapper(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                           RADIOLIB_PIN_TYPE busy)
        : STM32WLx_Module() {};
};
#endif

class RadioLibInterface : public RadioInterface, protected concurrency::NotifiedWorkerThread
{
    MeshPacketQueue txQueue = MeshPacketQueue(MAX_TX_QUEUE);

  protected:
    /// Used as our notification from the ISR
    enum PendingISR { ISR_NONE = 0, ISR_RX, ISR_TX, TRANSMIT_DELAY_COMPLETED, ISR_POLL_TICK };

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrTxLevel0(), isrLevel0Common(PendingISR code);

    ModemType_t modemType = RADIOLIB_MODEM_LORA;
    DataRate_t getDataRate() const { return {.lora = {.spreadingFactor = sf, .bandwidth = bw, .codingRate = cr}}; }
    PacketConfig_t getPacketConfig() const
    {
        return {.lora = {.preambleLength = preambleLength,
                         .implicitHeader = false,
                         .crcEnabled = true,
                         // We use auto LDRO, meaning it is enabled if the symbol time is >= 16msec
                         .ldrOptimize = (1 << sf) / bw >= 16}};
    }

    /**
     * We use a meshtastic sync word, but hashed with the Channel name.  For releases before 1.2 we used 0x12 (or for very old
     * loads 0x14) Note: do not use 0x34 - that is reserved for lorawan
     *
     * We now use 0x2b (so that someday we can possibly use NOT 2b - because that would be funny pun).  We will be staying with
     * this code for a long time.
     */
    const uint8_t syncWord = 0x2b;

    float currentLimit = 100; // 100mA OCP - Should be acceptable for RFM95/SX127x chipset.

#if !defined(USE_STM32WLx)
    Module module; // The HW interface to the radio
#else
    STM32WLx_ModuleWrapper module;
#endif

    /// Apply MESHTASTIC_RADIOLIB_SPI_TIMEOUT_MS. Called after begin(), so its calibration keeps RadioLib's default.
    void boundBusyWait()
    {
        if (MESHTASTIC_RADIOLIB_SPI_TIMEOUT_MS)
            module.spiConfig.timeout = MESHTASTIC_RADIOLIB_SPI_TIMEOUT_MS;
    }

    /**
     * provides lowest common denominator RadioLib API
     */
    PhysicalLayer *iface;

    /// are _trying_ to receive a packet currently (note - we might just be waiting for one)
    bool isReceiving = false;

    /// has the radio IRQ ever been armed? latches true and is never cleared, so ISR context only reads it
    volatile bool isrEverArmed = false;

  protected:
    // Noise floor tracking - rolling window of samples.
    static const uint8_t NOISE_FLOOR_SAMPLES = 20;
    static const int32_t NOISE_FLOOR_DEFAULT = -120;
    static const int32_t NOISE_FLOOR_VALID_MIN = -127;
    static const int32_t NOISE_FLOOR_INVALID = -128;
    int32_t noiseFloorSamples[NOISE_FLOOR_SAMPLES];
    uint8_t currentSampleIndex = 0;
    bool isNoiseFloorBufferFull = false;
    uint32_t lastNoiseFloorUpdate = 0;
    static const uint32_t NOISE_FLOOR_UPDATE_INTERVAL_MS = 5000;
    int32_t currentNoiseFloor = NOISE_FLOOR_DEFAULT;

    /**
     * Pure virtual hook for derived radio interfaces to provide instantaneous RSSI.
     * Implementations should return dBm, or an invalid value that updateNoiseFloor()
     * can reject.
     */
    virtual int16_t getCurrentRSSI() = 0;

  public:
    /** Our ISR code currently needs this to find our active instance
     */
    static RadioLibInterface *instance;

    /** Clear instance on destruction so stale pointer checks in loop() are safe */
    virtual ~RadioLibInterface()
    {
        if (instance == this)
            instance = nullptr;
    }

    /**
     * Get the current calculated noise floor in dBm
     * Returns -120 dBm if not yet calibrated
     */
    int32_t getNoiseFloor();

    /**
     * Calculate the average noise floor from collected samples
     */
    int32_t getAverageNoiseFloor();

    /**
     * Glue functions called from ISR land
     *
     * Skip the detach until the IRQ has been armed once: the first setStandby() runs before any
     * enableInterrupt(), and ESP-IDF logs "GPIO isr service is not installed" for that call.
     */
    void disableInterrupt()
    {
        if (!isrEverArmed)
            return;
        clearRadioIsr();
    }

    /**
     * Enable a particular ISR callback glue function
     */
    void enableInterrupt(void (*callback)())
    {
        // Latch before arming: the ISR can fire the moment the handler is installed.
        isrEverArmed = true;
        setRadioIsr(callback);
    }

    /**
     * Poll as a backup to catch missed edge-triggered interrupts.
     */
    void pollMissedIrqs();

    // Time::getMillis() at which a CAD->RX handoff left the chip listening without us arming it, or 0
    // if none is outstanding. 0 is a sentinel, so it must be tested before any elapsed comparison.
    uint32_t cadHandoffRxStart = 0;

    // True between the handoff and the rearmReceive() that consumes it: the chip is already in RX, so
    // that one re-arm must not standby. Both fields are cleared by setStandby().
    bool cadHandedToRx = false;

    /** Record that CAD left the chip in RX: arms both the flag and the no-show window below. */
    void noteCadHandoffToRx();

    /** True where DIO1 is only seen through libch341's pin poll (a CH341 USB host), so an interrupt arrives up to
     *  one poll interval after the chip raised it, and every command is a USB round trip. */
    bool irqPolledOverUsb() const;

    /** Re-arm if a CAD->RX handoff has produced no packet well past one max-length airtime. */
    void checkCadHandoffTimeout();

    // Time::getMillis() when plain RX was first seen holding PREAMBLE/HEADER flags, or 0 if none.
    uint32_t rxFlagsSeenMs = 0;
    // rxFlagsSeenMs was stamped by a header, not by a bare preamble before it
    bool rxFlagsSeenHeader = false;

    /** Plain-RX twin of checkCadHandoffTimeout(): retire flags no RX_DONE consumed within a max packet. */
    virtual void checkStaleRxFlags();

    /**
     * Reset AGC by power-cycling the analog frontend.
     * Subclasses override with chip-specific calibration sequences.
     * Safe to call periodically - skips if currently sending or receiving.
     * @return false if it skipped the reset or could not complete it, so the next maintenance tick retries
     */
    virtual bool resetAGC();

    /** Periodic radio upkeep: re-arms RX if a failed startReceive() left it off, otherwise resets AGC when it is due. */
    void periodicRadioMaintenance();

    /** Chip-specific recovery of a chip that lost its state to a reset/brownout. Returns true if reprogrammed. */
    virtual bool recoverChipStateLoss() { return false; }

    /** Throttled recoverChipStateLoss(), so a dead chip can't stall the RX/TX hot paths with repeated begin(). */
    bool maybeRecoverChipStateLoss();

    uint32_t lastChipRecoveryMs = 0;

    /// Consecutive recovery attempts that never got RX armed again, before rebooting to re-run init()
    static constexpr uint8_t MAX_CHIP_RECOVERY_FAILURES = 5;
    uint8_t chipRecoveryFailures = 0;

    /// Set by a driver's startReceive() when it gives up and leaves RX off; cleared once RX is armed again.
    bool rxOffline = false;

    /**
     * Debugging counts
     */
    uint32_t rxBad = 0, rxGood = 0, txGood = 0, txRelay = 0;
    uint32_t lastRxGoodMs = 0, lastAgcResetMs = 0; // 0: none yet
    uint16_t txDrop = 0;

  public:
    RadioLibInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                      RADIOLIB_PIN_TYPE busy, PhysicalLayer *iface = NULL);

    virtual ErrorCode send(meshtastic_MeshPacket *p) override;

    /**
     * Return true if we think the board can go to sleep (i.e. our tx queue is empty, we are not sending or receiving)
     *
     * This method must be used before putting the CPU into deep or light sleep.
     * With deepSleep set, an in-flight transmission also vetoes sleep (see RadioInterface).
     */
    virtual bool canSleep(bool deepSleep) override;

    /**
     * Start waiting to receive a message
     *
     * External functions can call this method to wake the device from sleep.
     * Subclasses must override and call this base method
     */
    virtual void startReceive();

    /**
     * Re-arm RX after a busy-channel CAD detect or after servicing an RX_DONE. Normally a full
     * startReceive(); after a CAD->RX handoff the chip is already listening, so that one re-arm
     * re-attaches the MCU ISR only - a startReceive() there would standby over the packet CAD found.
     */
    void rearmReceive();

    /** Resume an RX the chip is still running instead of restarting it; false if it is not known to be running. */
    virtual bool resumeRunningReceive() { return false; }

    /** Whether a TX payload written into the chip's buffer while this frame arrived can have overwritten part of it */
    virtual bool rxFrameOverlapsTxStage(size_t length) { return false; }

    /** A backoff shorter than this is waited out as before, and its payload staged at the scan */
    static constexpr uint32_t TX_STAGE_EARLY_MIN_MS = 5;
    /** When the TX timer really falls due, 0 if it was not brought forward to stage the payload */
    uint32_t txStageDueMs = 0;
    /** Whether a payload can be written during its backoff (checked from the thread that queues it) */
    virtual bool wantsEarlyTxStage() const { return false; }
    /** Write the next packet's payload while RX runs, ahead of its scan */
    virtual void stageTxEarly(meshtastic_MeshPacket *p) {}
    /** notifyLater(delay, TRANSMIT_DELAY_COMPLETED), brought forward where the payload can be staged early */
    void scheduleTransmitDelayCompleted(uint32_t delay);

    /** can we detect a LoRa preamble on the current channel?
     *  A true return means the chip may have been handed to RX in place, so the caller MUST follow it
     *  with rearmReceive() before anything else touches the radio. */
    virtual bool isChannelActive() = 0;

    /** are we actively receiving a packet (only called during receiving state)
     *  This method is only public to facilitate debugging.  Do not call.
     */
    virtual bool isActivelyReceiving() = 0;

    /** Are we are currently sending a packet?
     * This method is public, intending to expose this information to other firmware components
     */
    virtual bool isSending();

    /** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
    virtual bool cancelSending(NodeNum from, PacketId id) override;

    /** Attempt to find a packet in the TxQueue. Returns true if the packet was found. */
    virtual bool findInTxQueue(NodeNum from, PacketId id) override;

    uint8_t packetsInTxQueue() { return txQueue.getMaxLen() - txQueue.getFree(); }

    /**
     * Update the noise floor measurement by sampling RSSI from a slow path.
     * This should not be called from radio interrupt or TX/RX critical paths.
     */
    void updateNoiseFloor();

    /**
     * Check if we have collected any noise floor samples
     */
    bool hasNoiseFloorSamples();

    /**
     * Get the number of samples in the rolling window
     */
    uint8_t getNoiseFloorSampleCount();

    /**
     * Reset the noise floor calibration
     * Will automatically restart collection
     */
    void resetNoiseFloor();

    /**
     * Request randomness sourced from the LoRa modem, if supported by the active RadioLib interface.
     * @return true if len bytes were produced, false otherwise.
     */
    bool randomBytes(uint8_t *buffer, size_t length);

  private:
    uint8_t getNoiseFloorSampleCountInternal() const;
    int32_t getAverageNoiseFloorInternal() const;

    /** if we have something waiting to send, start a short (random) timer so we can come check for collision before actually
     * doing the transmit */
    void setTransmitDelay();

    /**
     * random timer with certain min. and max. settings
     * @return Timestamp after which the packet may be sent
     */
    void startTransmitTimer(bool withDelay = true);

    /**
     * timer scaled to SNR of to be flooded packet
     * @return Timestamp after which the packet may be sent
     */
    void startTransmitTimerRebroadcast(meshtastic_MeshPacket *p);

    /** Detach the sent packet and undo its pre-TX switch; the caller re-arms RX, then finishSentPacket(). */
    meshtastic_MeshPacket *handleTransmitInterrupt();

    /** A frame the readout task took out of the radio, and what addReceiveMetadata() would have read with it */
    struct CapturedRxInfo {
        int32_t rssi;
        float snr;
        int16_t state; // readData()'s result
        uint16_t len;
        uint8_t rxCR;         // the LoRa header's raw coding rate, read with the frame
        bool hasCRC;          // the LoRa header's CRC flag, likewise
        bool headerInfoValid; // false where the chip would not report them
    };

    /** Read out and deliver the frame behind RX_DONE; with captured, deliver one the readout task already took */
    void handleReceiveInterrupt(const CapturedRxInfo *captured = nullptr);

    /** Set across the getPacketTime() of a captured frame, so computePacketTime() takes that frame's header info
     *  instead of reading the chip. Only ever set and cleared on the radio thread, inside handleReceiveInterrupt(). */
    const CapturedRxInfo *rxCapturedHeader = nullptr;

    static void timerCallback(void *p1, uint32_t p2);

    virtual void onNotify(uint32_t notification) override;

    /** start an immediate transmit
     *  This method is virtual so subclasses can hook as needed, subclasses should not call directly
     *  @return true if packet was sent
     */
    virtual bool startSend(meshtastic_MeshPacket *txp);

    meshtastic_QueueStatus getQueueStatus();

  protected:
    uint32_t activeReceiveStart = 0;
    // Time::getMillis() when a look cleared PREAMBLE_DETECTED and began holding TX, or 0 if no hold.
    uint32_t preambleHoldStart = 0;

    /** True while a cleared preamble still holds TX; ends the hold once one max packet has passed. */
    bool preambleHoldActive();

    /** Clear a bare PREAMBLE_DETECTED and hold TX one max packet, unless a hold is already running. */
    void holdOnPreamble();

    /** Whether a packet is waiting to transmit; txQueue itself stays private. */
    bool hasQueuedTx() { return !txQueue.empty(); }

    bool receiveDetected(uint16_t irq, unsigned long syncWordHeaderValidFlag, unsigned long preambleDetectedFlag);

    /** Do any hardware setup needed on entry into send configuration for the radio.
     * Subclasses can customize, but must also call this base method */
    virtual void configHardwareForSend();

    /** Put radioBuffer's first numbytes on air; a subclass may launch a payload it staged during the scan */
    virtual int16_t launchTransmit(size_t numbytes);

    /** The packet the running channel scan is clearing the way for, so the scan can stage it; null otherwise */
    meshtastic_MeshPacket *scanForTx = nullptr;

    /** Could we send right now (i.e. either not actively receiving or transmitting)? */
    virtual bool canSendImmediately();

    /** busyRx deferrals since the last line, and when that line went out (0 = never) */
    uint32_t lastBusyRxLogMs = 0;
    uint32_t busyRxDeferred = 0;

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrRxLevel0();

    /**
     * If a send was in progress finish it and return the buffer to the pool */
    void completeSending();

    /** Clear sendingPacket and release its per-packet radio state; returns the packet, or null. */
    meshtastic_MeshPacket *detachSentPacket();

    /** Airtime, counters, log and pool release for a packet detachSentPacket() returned. */
    void finishSentPacket(meshtastic_MeshPacket *p);

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(meshtastic_MeshPacket *mp) = 0;

    /** Chip specific arm/disarm of the radio IRQ; call enableInterrupt()/disableInterrupt() instead */
    virtual void setRadioIsr(void (*callback)()) = 0;
    virtual void clearRadioIsr() = 0;

    /**
     * Subclasses must override, implement and then call into this base class implementation
     */
    virtual void setStandby();

    /** The LoRa header's raw coding rate and CRC flag for the frame the chip holds now; false where it cannot report
     *  them. Read with the frame, not later: on a chip left listening the status describes whatever it is receiving. */
    virtual bool readRxHeaderInfo(uint8_t &, bool &) { return false; }

    /** Drop what the chip still holds of a frame nobody will read out. Only a chip that receives into a FIFO keeps it:
     *  there the next readout would start with the dropped frame's bytes. */
    virtual void discardUnreadRxFrame() {}

    /// RadioLib returns its negative RADIOLIB_ERR_* codes through the same unsigned microsecond count it
    /// returns durations in, so an error reads as 4294967ms of airtime for one packet and takes the node
    /// off the air until it reboots (#11935). The codes are int16_t, so they wrap to the top of the
    /// range; the slowest packet we can configure is ~229s, well clear of it.
    static bool isRadioLibTimeError(RadioLibTime_t usec) { return usec == 0 || usec >= (RadioLibTime_t)0 - 32768; }

    /**
     * Derive packet time either for a received (using header info) or a transmitted packet
     */
    template <typename T> uint32_t computePacketTime(T &lora, uint32_t pl, bool received)
    {
        DataRate_t dr = getDataRate();
        PacketConfig_t pc = getPacketConfig();

        if (received) {
            // Received packet configuration must be the same as configured, except for coding rate and CRC
            uint8_t rxCR = 0;
            bool hasCRC = true;
            // A frame the readout task took carries its own header info, read off the chip with it. Asking the chip
            // here would read the status of whatever it is receiving now, since the resume leaves it listening.
            bool haveHeaderInfo;
            if (rxCapturedHeader) {
                haveHeaderInfo = rxCapturedHeader->headerInfoValid;
                rxCR = rxCapturedHeader->rxCR;
                hasCRC = rxCapturedHeader->hasCRC;
            } else {
                RadioSequence seq(this); // a chip read, from callers that hold no sequence: see getTimeOnAir() below
                haveHeaderInfo = lora.getLoRaRxHeaderInfo(&rxCR, &hasCRC) == RADIOLIB_ERR_NONE;
            }
            if (haveHeaderInfo) {
                // Raw 0 is reserved and >7 is either undefined or an LR2021-only convolutional rate no
                // Meshtastic peer can send. calculateTimeOnAir() would multiply by it unchecked.
                if (rxCR < 1 || rxCR > 7) {
                    LOG_WARN("Bogus RX coding rate %d from radio, use configured %d", rxCR, dr.lora.codingRate);
                } else {
                    // Go from raw header value to denominator
                    if (rxCR < 5) {
                        rxCR += 4;
                    } else if (rxCR == 7) {
                        rxCR = 8;
                    }

                    dr.lora.codingRate = rxCR;
                    pc.lora.crcEnabled = hasCRC;
                }
            }
        } else {
            // Reads the packet type back over SPI, so a chip that lost its config answers WRONG_MODEM. Called after every
            // send by finishSentPacket() and the router, which hold no sequence: unlocked, a readout could land inside it.
            RadioLibTime_t reported;
            {
                RadioSequence seq(this);
                reported = lora.getTimeOnAir(pl);
            }
            if (!isRadioLibTimeError(reported))
                return reported / 1000;
            LOG_WARN("%s%d from getTimeOnAir, use configured modem", radioLibErr, (int)(int16_t)reported);
        }

        // Arithmetic on the config we asked for, with no readback to fail. Guarded too: once a code is
        // in milliseconds nothing downstream can tell it from a duration.
        RadioLibTime_t computed = lora.calculateTimeOnAir(modemType, dr, pc, pl);
        if (isRadioLibTimeError(computed)) {
            LOG_ERROR("%s%d from calculateTimeOnAir", radioLibErr, (int)(int16_t)computed);
            return 0;
        }

        return computed / 1000;
    }

    const char *radioLibErr = "RadioLib err=";

    /// TCXO_STARTUP_DELAY_US, or Lora.DIO3_TCXO_DELAY_US on Portduino
    static uint32_t tcxoStartupDelayUs();

    /// Reprogram the TCXO start-up delay, which begin() resets to RadioLib's 5000 us. No-op without a TCXO Vref.
    template <typename R> void applyTcxoStartupDelay(R &radio, float tcxoVoltage)
    {
        if (tcxoVoltage <= 0)
            return;
        const uint32_t delayUs = tcxoStartupDelayUs();
        const int16_t err = radio.setTCXO(tcxoVoltage, delayUs);
        if (err == RADIOLIB_ERR_NONE)
            LOG_DEBUG("TCXO start-up delay %u us", (unsigned)delayUs);
        else
            LOG_WARN("TCXO start-up delay %u us not set %s%d", (unsigned)delayUs, radioLibErr, err);
    }

    /**
     * If the packet is not already in the late rebroadcast window, move it there
     */
    void clampToLateRebroadcastWindow(NodeNum from, PacketId id);

    /**
     * If there is a packet pending TX in the queue with a worse hop limit, remove it pending replacement with a better version
     * @return Whether a pending packet was removed
     */

    bool removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt) override;

    /** Retire RX_DONE and CRC_ERR, and the chip's copy of the frame, for a frame nothing will read out. readData() clears
     * its own, so this is for the early outs in handleReceiveInterrupt() that return before it: left latched, they would
     * be taken for an unread frame and re-notified for as long as they sit there. */
    void clearReadIrqs();

    /** @return whether a latched RX_DONE was found and notified, so a caller can say which look caught it */
    bool checkRxDoneIrqFlag();
    void checkTxDoneIrqFlag();

    /** From the TX_DONE interrupt, put the chip straight back into RX; false if it did not */
    virtual bool rearmReceiveFromIsr() { return false; }

    /** After TX, take over the RX that rearmReceiveFromIsr() started instead of restarting it; false if there is none */
    virtual bool adoptReceiveArmedFromIsr() { return false; }

    /** Software-poll substitute for a hardware DIO interrupt, for radios whose IRQ line sits behind
     * an I2C IO expander with no INT routed to the MCU (e.g. Meshnology W10, LORA_DIO1_SOFTWARE_POLL).
     * The chip-specific subclass polls the radio's IRQ status register from the radio thread and
     * synthesizes ISR_TX/ISR_RX events equivalent to the hardware DIO1 interrupt. */
    void deliverPendingIrqFromPoll(PendingISR cause);
    void scheduleIrqPollTick();
    static bool isIsrTxCallback(void (*callback)());
    virtual void handleSoftwareLoraIrqPoll() {}

    /** Take the radio-sequence lock, returning whether it was actually taken - see RadioSequence. The lock is
     *  created with the readout task, so a sequence that started before it has nothing to take, and must not
     *  then release it. Without a readout task at all, both are no-ops. */
#ifdef MESHTASTIC_RX_READOUT_TASK
    bool lockRadioSequence();
    void unlockRadioSequence();
#else
    bool lockRadioSequence() { return false; }
    void unlockRadioSequence() {}
#endif

    /** Held across a whole RadioLib call sequence, so the readout task cannot run between its calls.
     *
     *  The SPI lock is per transaction, so it does not span a sequence, and every driver's readData() ends by
     *  clearing the chip's IRQ flags - SX128x also drops to standby first. A readout landing inside a channel
     *  scan, an RX arm, a transmit setup or a reconfigure would therefore clear flags that sequence is about to
     *  rely on, or move the chip out from under it.
     *
     *  The lock is recursive, because these sequences nest, and it inherits priority, so a readout waiting on
     *  the radio thread lifts it rather than sitting behind it. Holding it does NOT bound a readout by the whole
     *  main loop - only by the radio call sequence in flight, which is why packet delivery stays outside it.
     *
     *  Lock order: this lock is always taken BEFORE the SPI lock, never while holding it. RadioLib takes the SPI
     *  lock per transaction inside the calls a sequence makes, so every holder acquires them in that order; a
     *  caller that took the SPI lock first and then entered a sequence would invert it. */
    class RadioSequence
    {
      public:
        explicit RadioSequence(RadioLibInterface *iface) : iface(iface), held(iface->lockRadioSequence()) {}
        ~RadioSequence()
        {
            if (held)
                iface->unlockRadioSequence();
        }
        RadioSequence(const RadioSequence &) = delete;
        RadioSequence &operator=(const RadioSequence &) = delete;

      private:
        RadioLibInterface *iface; // declared before held: held's initializer calls through it
        bool held;
    };

#ifdef MESHTASTIC_RX_READOUT_TASK
    /** From the RX_DONE interrupt, wake the readout task; false if there is none. The interrupt stays enabled, and the
     *  task notifies ISR_RX once the frame is out of the radio. */
    bool rxDoneFromIsr();
    bool rxReadoutActive() const { return rxReadoutTask != nullptr; }
    /** Hand an RX_DONE found by a poll to the readout task; false if there is no task. Does not wait: the
     *  radio-sequence lock, not a wait here, is what keeps the task out of the caller's RadioLib calls. */
    bool wakeRxReadout();
    /** Deliver every frame the readout task captured; returns how many */
    unsigned deliverCapturedFrames();
    /** From the TX_DONE interrupt, have the readout task call rearmReceiveFromTask() before anything else; false if there
     *  is no task. For drivers that re-arm RX through RadioLib, which an interrupt cannot call. */
    bool requestRearmFromIsr();
    /** The readout task's half of requestRearmFromIsr() */
    virtual void rearmReceiveFromTask() {}

  private:
    /** Start the readout task above the calling task (the main loop), once */
    void startRxReadoutTask();
    static void rxReadoutTaskMain(void *arg);
    /** One readout, from the task, with the RadioLib calls handleReceiveInterrupt() makes */
    void readOutFromTask();
    /** Move the oldest captured frame into radioBuffer; false if there is none */
    bool takeCapturedFrame(CapturedRxInfo &info);

    TaskHandle_t rxReadoutTask = nullptr;
    bool rxReadoutTaskTried = false;
    /** Recursive, priority-inheriting; guards a whole RadioLib call sequence - see RadioSequence */
    SemaphoreHandle_t radioSeqMutex = nullptr;
    /** Captured frames: the task produces, the radio thread consumes */
    struct CapturedFrame {
        CapturedRxInfo info;
        uint8_t data[sizeof(RadioBuffer)];
    };
    static constexpr uint8_t rxRingSize = 9; // holds 8, for frames that end back to back behind a long main-loop hold
    CapturedFrame rxRing[rxRingSize] = {};
    volatile uint8_t rxRingHead = 0, rxRingTail = 0;
    volatile uint32_t rxReadoutFrames = 0, rxReadoutDropped = 0, rxReadoutBadLength = 0;
    /** Set by requestRearmFromIsr(), taken by the task */
    volatile bool rxRearmFromTaskPending = false;

  protected:
    /** The task re-armed RX at TX_DONE and the radio thread has not yet handled that TX_DONE: until it does, a frame the
     *  task reads must not overwrite the pending ISR_TX, or the TX is never completed. onNotify() delivers it instead. */
    volatile bool rxArmedBeforeTxDone = false;
#else
    bool rxDoneFromIsr() { return false; }
    bool requestRearmFromIsr() { return false; }
    bool rxReadoutActive() const { return false; }
    bool wakeRxReadout() { return false; }
    unsigned deliverCapturedFrames() { return 0; }
#endif
};
