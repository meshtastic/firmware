#pragma once

#include "MeshPacketQueue.h"
#include "RadioInterface.h"
#include "UptimeClock.h"
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

// In addition to the default Rx flags, we need the PREAMBLE_DETECTED flag to detect whether we are actively receiving
#define MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED))

#define AGC_RESET_INTERVAL_MS (60 * 1000) // 60 seconds

/// What the radio's latched RX flags have shown since the last standby, stamped at each look at them.
/// The owner must clear PREAMBLE_DETECTED whenever a look finds it, so every sighting is a new detection.
class RxSighting
{
  public:
    /// Record one look at the flags; returns whether a frame may be on air, so TX should wait.
    bool observe(uint32_t nowMsec, bool preamble, bool header, uint32_t maxPacketMsec)
    {
        const uint32_t now = lastPeekMsec = Time::skipZero(nowMsec);
        if (preamble)
            preambleSeenMsec = now;
        if (header && !headerSeenMsec)
            headerSeenMsec = now;
        // Neither flag says when its frame ends; only RX_DONE (via reset()) or one max packet from the sighting does.
        if (preambleSeenMsec && now - preambleSeenMsec >= maxPacketMsec)
            preambleSeenMsec = 0;
        // A header is kept past expiry so that the same latch, left by a missed RX IRQ, cannot re-arm the hold.
        const bool headerHolds = headerSeenMsec && now - headerSeenMsec < maxPacketMsec;
        return headerHolds || preambleSeenMsec;
    }

    /// Standby and RX start clear the chip's flags, so they clear this too.
    void reset() { lastPeekMsec = preambleSeenMsec = headerSeenMsec = 0; }

    uint32_t lastPeek() const { return lastPeekMsec; }
    uint32_t preambleSeen() const { return preambleSeenMsec; }
    uint32_t headerSeen() const { return headerSeenMsec; }

  private:
    uint32_t lastPeekMsec = 0;     // last look at the flags, 0 if none since reset
    uint32_t preambleSeenMsec = 0; // last look that found a fresh PREAMBLE_DETECTED, 0 once its hold ends
    uint32_t headerSeenMsec = 0;   // first look that found HEADER_VALID, 0 if none since reset
};

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
    enum PendingISR { ISR_NONE = 0, ISR_RX, ISR_TX, TRANSMIT_DELAY_COMPLETED, ISR_POLL_TICK, TX_DONE_CHECK };

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrTxLevel0(), isrLevel0Common(PendingISR code);

#ifdef ARCH_PORTDUINO
    // millis() at the last radio interrupt, for the RX latency trace on a CH341 host (where the "ISR" is libch341's
    // pin poll thread)
    static volatile uint32_t lastIsrMillis;
#endif

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

    /** True where DIO1 is only seen through libch341's 30 Hz pin poll (a CH341 USB host), so an
     *  interrupt arrives 0-35 ms after the chip raised it. */
    bool irqPolledOverUsb() const;

    // Timed TX_DONE check for irqPolledOverUsb() hosts: the chip drops to standby when a frame ends and is
    // deaf until we notice, so look when the frame should have ended instead of waiting for the poll.
    static constexpr uint32_t TX_DONE_CHECK_MARGIN_MS = 1;
    static constexpr uint32_t TX_DONE_RECHECK_MS = 2;
    static constexpr uint8_t TX_DONE_CHECK_TRIES = 4;
    uint8_t txDoneChecksLeft = 0;
    // Set when the timed check completed a TX, so the poll's late copy of the same edge is dropped.
    bool txDoneByCheck = false;
    void checkTxDone();

    /** Re-arm if a CAD->RX handoff has produced no packet well past one max-length airtime. */
    void checkCadHandoffTimeout();

    // Time::getMillis() when plain RX was first seen holding PREAMBLE/HEADER flags, or 0 if none.
    uint32_t rxFlagsSeenMs = 0;

    /** Plain-RX twin of checkCadHandoffTimeout(): retire flags no RX_DONE consumed within a max packet. */
    virtual void checkStaleRxFlags();

    /**
     * Reset AGC by power-cycling the analog frontend.
     * Subclasses override with chip-specific calibration sequences.
     * Safe to call periodically - skips if currently sending or receiving.
     */
    virtual void resetAGC();

    /** Periodic radio upkeep: re-arms RX if a failed startReceive() left it off, otherwise resets AGC. */
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

    /** Bench: from the TX_DONE interrupt, put the chip straight back into RX; false if it did not. */
    virtual bool rearmReceiveFromIsr() { return false; }

    /** Bench: after TX, take over the RX rearmReceiveFromIsr() started instead of restarting it; false if there is none. */
    virtual bool adoptReceiveArmedFromIsr() { return false; }

    /** Bench: a frame a readout task took from the chip, and what it saw; the frame itself goes into radioBuffer */
    struct CapturedRxInfo {
        uint32_t wakeMs;    // millis() of the RX_DONE interrupt, or of the poll that found RX_DONE
        uint32_t readMs;    // millis() when the readout ended
        uint32_t spiUs;     // SPI time of the readout, lock wait excluded
        int32_t rssi;       // as getRSSI() would report
        float snr;          // as getSNR() would report
        int16_t state;      // as readData() would report: RADIOLIB_ERR_NONE or RADIOLIB_ERR_CRC_MISMATCH
        uint8_t len;        // bytes in the frame
        bool chipListening; // the chip was still in RX after the frame, so nothing needs re-arming
    };

    /** Bench: from the RX_DONE interrupt, hand the readout to a task; true if one took it. The interrupt then stays
     *  enabled, and the task notifies ISR_RX once the frame is out of the chip. */
    virtual bool rxDoneFromIsr() { return false; }

    /** Bench: whether a readout task takes RX_DONE instead of this thread */
    virtual bool rxReadoutActive() const { return false; }

    /** Bench: wake the readout task from this thread, for an RX_DONE found by a poll. The task runs above this thread,
     *  so the frame is normally out of the chip when this returns. False if there is no task. */
    virtual bool wakeRxReadout() { return false; }

    /** Bench: move the oldest frame the readout task captured into radioBuffer; false if there is none */
    virtual bool takeCapturedFrame(CapturedRxInfo &info)
    {
        (void)info;
        return false;
    }

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

    void handleTransmitInterrupt();
    /** Read out and deliver the frame behind RX_DONE; with captured, deliver one a readout task already took */
    void handleReceiveInterrupt(const CapturedRxInfo *captured = nullptr);
    /** handleReceiveInterrupt()'s side of the chip: the RX bookkeeping, and the frame's length; false if there is
     *  nothing to read */
    bool beginReceiveFromChip(size_t &length);
    /** Bench: deliver every frame the readout task captured; sets *rxEnded if the chip had left RX after one */
    unsigned deliverCapturedFrames(bool *rxEnded);

    static void timerCallback(void *p1, uint32_t p2);

    virtual void onNotify(uint32_t notification) override;

    /** start an immediate transmit
     *  This method is virtual so subclasses can hook as needed, subclasses should not call directly
     *  @return true if packet was sent
     */
    virtual bool startSend(meshtastic_MeshPacket *txp);

    meshtastic_QueueStatus getQueueStatus();

  protected:
    /// When the radio last began work that leaves it unable to receive, and what that work was, so the
    /// next startReceive() can report how long it was deaf. 0 when nothing is pending.
    uint32_t deafSinceMs = 0;
    const char *deafFor = nullptr;
    void noteDeafFrom(const char *what);

    /** How long the last startReceive() spent in each part, in ms, for the post-TX re-arm trace */
    struct RxArmSteps {
        uint32_t standbyMs, standbyCmdMs, startRxMs, armMs;
    } lastRxArmSteps = {0, 0, 0, 0};

    RxSighting rxSighting;

    /** Airtime of the longest frame we could be receiving: 255 bytes at CR 4/8 with CRC, whatever our own CR. */
    uint32_t maxRxFrameMsec();

    /** Record a look at the RX flags and clear a PREAMBLE_DETECTED it found; true while a frame may be on air. */
    bool receiveDetected(uint16_t irq, unsigned long syncWordHeaderValidFlag, unsigned long preambleDetectedFlag);

    /** Do any hardware setup needed on entry into send configuration for the radio.
     * Subclasses can customize, but must also call this base method */
    virtual void configHardwareForSend();

    /** Put radioBuffer's first numbytes on air; a subclass may launch a payload it staged before the scan */
    virtual int16_t launchTransmit(size_t numbytes);

    /** The packet the running channel scan is clearing the way for, so the scan can stage it; null otherwise */
    meshtastic_MeshPacket *scanForTx = nullptr;

    /** Could we send right now (i.e. either not actively receiving or transmitting)? */
    virtual bool canSendImmediately();

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrRxLevel0();

    /**
     * If a send was in progress finish it and return the buffer to the pool */
    void completeSending();

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
            if (lora.getLoRaRxHeaderInfo(&rxCR, &hasCRC) == RADIOLIB_ERR_NONE) {
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
            // Reads the packet type back over SPI, so a chip that lost its config answers WRONG_MODEM.
            RadioLibTime_t reported = lora.getTimeOnAir(pl);
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

    /**
     * If the packet is not already in the late rebroadcast window, move it there
     */
    void clampToLateRebroadcastWindow(NodeNum from, PacketId id);

    /**
     * If there is a packet pending TX in the queue with a worse hop limit, remove it pending replacement with a better version
     * @return Whether a pending packet was removed
     */

    bool removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt) override;

    void checkRxDoneIrqFlag();
    void checkTxDoneIrqFlag();

    /** Software-poll substitute for a hardware DIO interrupt, for radios whose IRQ line sits behind
     * an I2C IO expander with no INT routed to the MCU (e.g. Meshnology W10, LORA_DIO1_SOFTWARE_POLL).
     * The chip-specific subclass polls the radio's IRQ status register from the radio thread and
     * synthesizes ISR_TX/ISR_RX events equivalent to the hardware DIO1 interrupt. */
    void deliverPendingIrqFromPoll(PendingISR cause);
    void scheduleIrqPollTick();
    static bool isIsrTxCallback(void (*callback)());
    virtual void handleSoftwareLoraIrqPoll() {}
};