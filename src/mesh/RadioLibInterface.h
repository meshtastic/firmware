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

// Bench: -DMESHTASTIC_RX_READOUT_TASK reads each received frame out of the radio from a FreeRTOS task woken by the
// RX_DONE interrupt, through RadioLib, for every radio. SX126X_RX_READOUT_TASK was its name while it was SX126x-only.
#if defined(SX126X_RX_READOUT_TASK) && !defined(MESHTASTIC_RX_READOUT_TASK)
#define MESHTASTIC_RX_READOUT_TASK
#endif

// Bench: -DLR11X0_TX_STAGE_EARLY and -DSX126X_TX_STAGE_EARLY write the TX payload during its backoff (see the
// driver headers). The timer plumbing lives here, as #12016's does.
#if (defined(LR11X0_TX_STAGE_EARLY) || defined(SX126X_TX_STAGE_EARLY)) && !defined(MESHTASTIC_TX_STAGE_EARLY)
#define MESHTASTIC_TX_STAGE_EARLY
#endif

// Bench: -DMESHTASTIC_TX_HOLD_FOR_CAD_RX holds TX while the RX a busy CAD handed off to is still open, so the next
// scan's standby does not abort the frame the CAD heard.

// In addition to the default Rx flags, we need the PREAMBLE_DETECTED flag to detect whether we are actively receiving
#define MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1 << RADIOLIB_IRQ_PREAMBLE_DETECTED))

// Bench: -DMESHTASTIC_REARM_HOLD_FIX=0 runs the TX_DONE re-arm as round 64 did: the RX interrupt stays detached until the
// radio thread adopts the task's re-arm, and the task re-arms whatever the state. Default 1 (ca5062ca1's behaviour).
#ifndef MESHTASTIC_REARM_HOLD_FIX
#define MESHTASTIC_REARM_HOLD_FIX 1
#endif

// Bench: the readout task reads a frame again when readData() fails with WRONG_MODEM (-20), which on the LR11x0 returns
// before touching the frame. -DMESHTASTIC_RX_RETRY_WRONG_MODEM=0 turns it off for an A/B. Default 1.
#ifndef MESHTASTIC_RX_RETRY_WRONG_MODEM
#define MESHTASTIC_RX_RETRY_WRONG_MODEM 1
#endif
#if MESHTASTIC_RX_RETRY_WRONG_MODEM
#define MESHTASTIC_RX_RETRY_MARK "on"
#else
#define MESHTASTIC_RX_RETRY_MARK "off"
#endif
// RadioLibInterface::rxCounters() and readChipRxStats() exist; the DMShell test client reads them when this is set
#define MESHTASTIC_BENCH_RX_COUNTERS 1

#define AGC_RESET_INTERVAL_MS (60 * 1000) // 60 seconds

// Bench: -DMESHTASTIC_LOG_RADIO_EDGES logs where the radio stops and starts hearing, and its preamble sightings, at DEBUG
// instead of TRACE: a few lines a second, against the full trace build's volume
#ifdef MESHTASTIC_LOG_RADIO_EDGES
#define LOG_RADIO_EDGE LOG_DEBUG
#else
#define LOG_RADIO_EDGE LOG_TRACE
#endif

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
#ifdef MESHTASTIC_SPI_CMD_ATOMIC
    // Bench: serialise whole chip commands, not just single transfers. See spiCmdLock in SPILock.h.
    void spiLockCommand() override;
    void spiUnlockCommand() override;
#endif
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

#ifdef MESHTASTIC_TX_SLOT_ANCHOR
    // Bench: FreeRTOS tick of the last TX_DONE and RX_DONE interrupts, for the slot anchor
    static volatile uint32_t txDoneIsrTicks, rxDoneIsrTicks;
    /** Bench: millis() when the frame being sent should leave the air, from its launch and its airtime */
    uint32_t txPredictedEndMs = 0;
    /** Bench: the best estimate of when the frame this TX_DONE or RX_DONE ended left the air */
    uint32_t frameEndFromIsr(bool tx);
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

#ifdef SX126X_TX_STAGE_IN_RX
    /** Bench: whether a TX payload staged while this frame arrived can have overwritten part of it */
    virtual bool rxFrameOverlapsTxStage(size_t length) { return false; }
#endif

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

    /** Bench: this firmware's receive counts, for the test client's session stats */
    struct RxCounters {
        uint32_t good, bad;            // packets handleReceiveInterrupt() passed on, and rejected
        uint32_t readOut, readOutLost; // frames the readout task took from the chip, and lost (ring full, bad length)
        uint32_t retried, recovered;   // readouts read again after WRONG_MODEM, and those the second read recovered
    };
    RxCounters rxCounters() const
    {
#ifdef MESHTASTIC_RX_READOUT_TASK
        return {rxGood, rxBad, rxReadoutFrames, rxReadoutDropped + rxReadoutBadLength, rxReadoutRetried, rxReadoutRecovered};
#else
        return {rxGood, rxBad, 0, 0, 0, 0};
#endif
    }

#ifdef MESHTASTIC_RX_FAIL_PROBE
    /** Bench: the chip's packet type, mode and IRQ flags, for a readout that just failed. False if unavailable. */
    virtual bool readRxFailState(uint8_t & /*pktType*/, uint8_t & /*mode*/, uint32_t & /*irq*/, int16_t & /*typeErr*/)
    {
        return false;
    }
#endif

    /** Bench: the chip's own receive counters since its last reset. False where the chip or RadioLib keeps none. */
    virtual bool readChipRxStats(uint16_t & /*received*/, uint16_t & /*crcError*/, uint16_t & /*headerError*/,
                                 uint16_t & /*falseSync*/)
    {
        return false;
    }

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

    /** Bench: a frame the readout task took from the chip, and what it saw; the frame itself goes into radioBuffer */
    struct CapturedRxInfo {
        uint32_t wakeMs;        // millis() of the RX_DONE interrupt, or of the poll that found RX_DONE
        uint32_t readMs;        // millis() when the readout ended
        uint32_t spiUs;         // time of the readout's RadioLib calls
        int32_t rssi;           // getRSSI()
        float snr;              // getSNR()
        int16_t state;          // readData()'s result
        uint8_t len;            // bytes in the frame
        bool chipListening;     // the driver's RX was still running after the frame, so nothing needs re-arming
        bool retried;           // the first readData() failed with WRONG_MODEM, so the frame was read again
        int16_t firstState;     // that first readData()'s result
        uint8_t firstLen;       // and the length read before it
        int16_t immediateState; // the read straight after the failure; state is the final one, after a tick if needed
        uint8_t immediateLen;
#ifdef MESHTASTIC_RX_FAIL_PROBE
        // Bench: the chip's own state at a FAILED readout, read in the task right after readData(). Only
        // meaningful when state != RADIOLIB_ERR_NONE. For -20 WRONG_MODEM the question is whether the chip
        // really reports another modem, or whether GetPacketType returned success without writing a reply,
        // leaving readData()'s initialiser: NONE is 0x00 on LR11x0 and 0xFF on LR2021.
        uint8_t failPktType;
        uint8_t failMode;
        uint32_t failIrq;
        int16_t failTypeErr;
        bool failValid;
#endif
    };

    /** Bench: whether the driver's RX keeps running after RX_DONE (a continuous RX), so a frame read out by the
     *  readout task needs no re-arm. False (re-arm, as without the task) unless the driver knows. */
    virtual bool receiveStillRunning() const { return false; }

#ifdef MESHTASTIC_RX_READOUT_TASK
    /** Bench: from the RX_DONE interrupt, wake the readout task; true if there is one. The interrupt then stays
     *  enabled, and the task notifies ISR_RX once the frame is out of the chip. */
    bool rxDoneFromIsr();

    /** Bench: whether the readout task takes RX_DONE instead of this thread */
    bool rxReadoutActive() const { return rxReadoutTask != nullptr; }

    /** Bench: wake the readout task from this thread, for an RX_DONE found by a poll, and wait for its readout (on
     *  one core it runs above this thread, so there is no wait). False if there is no task. */
    bool wakeRxReadout();

    /** Bench: move the oldest frame the readout task captured into radioBuffer; false if there is none */
    bool takeCapturedFrame(CapturedRxInfo &info);

    /** Bench: from the TX_DONE interrupt, have the readout task call rearmReceiveFromTask() before anything else; false if
     *  there is no task. For drivers that re-arm RX through RadioLib, which an interrupt cannot call. */
    bool requestRearmFromIsr();

    /** Bench: the readout task's half of requestRearmFromIsr(), run at the task's priority with the SPI lock free */
    virtual void rearmReceiveFromTask() {}
#else
    bool rxDoneFromIsr() { return false; }
    bool rxReadoutActive() const { return false; }
    bool wakeRxReadout() { return false; }
    bool takeCapturedFrame(CapturedRxInfo &) { return false; }
#endif

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

#ifdef MESHTASTIC_TX_STAGE_EARLY
    /** A backoff shorter than this is waited out as before, and its payload staged at the scan */
    static constexpr uint32_t TX_STAGE_EARLY_MIN_MS = 5;
    /** When the TX timer really falls due, 0 if it was not brought forward to stage the payload */
    uint32_t txStageDueMs = 0;
    /** Whether a payload can be written during its backoff (checked from the thread that queues it) */
    virtual bool wantsEarlyTxStage() const { return false; }
    /** Write the next packet's payload while RX runs, ahead of its scan */
    virtual void stageTxEarly(meshtastic_MeshPacket *) {}
#endif
    /** notifyLater(delay, TRANSMIT_DELAY_COMPLETED), brought forward where the payload can be staged early */
    void scheduleTransmitDelayCompleted(uint32_t delay);

#ifdef MESHTASTIC_RX_DEFER_FOR_TX_MS
    /** When the armed TX timer really falls due (not the early-stage fire), 0 once it has run */
    uint32_t txDueMs = 0;

  public:
    uint32_t getTxDueMs() const override { return txDueMs; }

  protected:
#endif

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

#ifdef MESHTASTIC_RX_READOUT_TASK
  private:
    /** Start the readout task, above the task calling (the loop), once */
    void startRxReadoutTask();
    static void rxReadoutTaskMain(void *arg);
    /** One readout through RadioLib, from the task: IRQ flags, length, readData(), SNR and RSSI */
    void readOutFromTask();
    TaskHandle_t rxReadoutTask = nullptr;
    bool rxReadoutTaskTried = false;
    /** FreeRTOS tick count of the last wake, from the interrupt or from a poll */
    volatile uint32_t rxWakeTicks = 0;
    /** Readouts the task has finished, for wakeRxReadout() to wait on */
    volatile uint32_t rxReadoutPasses = 0;
    /** Set by requestRearmFromIsr(), taken by the task */
    volatile bool rxRearmFromTaskPending = false;

  protected:
    /** The task re-armed RX at TX_DONE and the radio thread has not yet handled that TX_DONE: until it does, a frame the
     *  task reads must not overwrite the pending ISR_TX, or the TX is never completed. onNotify() delivers it instead. */
    volatile bool rxArmedBeforeTxDone = false;

  private:
    /** Frames read out, single producer (the task), single consumer (this thread) */
    struct CapturedFrame {
        CapturedRxInfo info;
        uint8_t data[sizeof(RadioBuffer)];
    };
    static constexpr uint8_t rxRingSize = 9; // holds 8: frames that end back to back behind a long main-loop hold
    CapturedFrame rxRing[rxRingSize];
    volatile uint8_t rxRingHead = 0, rxRingTail = 0;
    volatile uint32_t rxReadoutFrames = 0, rxReadoutDropped = 0, rxReadoutBadLength = 0;
    volatile uint32_t rxReadoutRetried = 0, rxReadoutRecovered = 0;
#endif
};