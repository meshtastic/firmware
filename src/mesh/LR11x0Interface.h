#pragma once
#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "RadioLibInterface.h"

// Bench: -DLR11X0_TX_LAUNCH_TRACE times each step from the CAD verdict to TX in microseconds, and -DLR11X0_TX_PRESTAGE also
// writes the payload before the scan. Prestage calls RadioLib's LR11x0 commands directly, so it needs -DRADIOLIB_GODMODE=1.
#if defined(LR11X0_TX_LAUNCH_TRACE) || defined(LR11X0_TX_PRESTAGE)
#define LR11X0_TX_LAUNCH_OVERRIDE 1
#endif
#if defined(LR11X0_TX_PRESTAGE) && !RADIOLIB_GODMODE
#error "LR11X0_TX_PRESTAGE calls RadioLib's LR11x0 commands directly: build with -DRADIOLIB_GODMODE=1"
#endif
// Bench probe: -DLR11X0_CAD_EXIT_PROBE scans with CAD exit mode 0x11 (RX on detection | TX when clear), which Semtech does
// not document, and logs the mode the chip lands in after each verdict. It keys up from the prestaged payload.
// Bench: -DLR11X0_CAD_EXIT_LBT scans with exit mode 0x10 (LBT): a clear CAD keys up from the prestaged payload, and a busy
// one leaves the chip in standby for rearmReceive() to restart RX, with no CAD>RX handoff. Same path as the probe.
#if defined(LR11X0_CAD_EXIT_PROBE) && defined(LR11X0_CAD_EXIT_LBT)
#error "LR11X0_CAD_EXIT_PROBE and LR11X0_CAD_EXIT_LBT pick different exit modes: build with one"
#endif
#if defined(LR11X0_CAD_EXIT_PROBE)
#define LR11X0_CAD_EXIT_KEYUP (RADIOLIB_LR11X0_CAD_EXIT_MODE_RX | RADIOLIB_LR11X0_CAD_EXIT_MODE_LBT)
#elif defined(LR11X0_CAD_EXIT_LBT)
#define LR11X0_CAD_EXIT_KEYUP RADIOLIB_LR11X0_CAD_EXIT_MODE_LBT
#endif
#if defined(LR11X0_CAD_EXIT_KEYUP) && !defined(LR11X0_TX_PRESTAGE)
#error "LR11X0_CAD_EXIT_PROBE and _LBT send the prestaged payload: build with -DLR11X0_TX_PRESTAGE -DRADIOLIB_GODMODE=1"
#endif
// Bench: -DLR11X0_RESUME_CONTINUOUS_RX keeps a continuous RX running after a frame instead of restarting it, checking the
// chip is still in RX first. -DLR11X0_RX_REARM_AT_TX_DONE re-arms RX at TX_DONE from the readout task, before the radio
// thread runs; the interrupt cannot call RadioLib, so it needs -DMESHTASTIC_RX_READOUT_TASK.
#if defined(LR11X0_RX_REARM_AT_TX_DONE) && !defined(MESHTASTIC_RX_READOUT_TASK)
#error "LR11X0_RX_REARM_AT_TX_DONE re-arms from the readout task: build with -DMESHTASTIC_RX_READOUT_TASK"
#endif
// Bench, shortening the scan's deaf window:
// -DLR11X0_TX_STAGE_EARLY writes the payload during the backoff while RX runs, not in the scan's standby. WriteBuffer8
//   fills the TX buffer and received frames land in the separate RX buffer (Semtech's lr11xx_regmem.h), so the write
//   aborts no frame and nothing received overwrites it. Needs -DLR11X0_TX_PRESTAGE.
// -DLR11X0_CAD_SLIM starts the CAD without RadioLib's packet-type reads and second standby, and sends the CAD parameters
//   only when they change.
// -DLR11X0_STANDBY_XOSC keeps the TCXO running in standby and the TX/RX fallback, so a CAD or RX started from them skips
//   the TCXO start-up. It reaches the CAD only with -DLR11X0_CAD_SLIM: RadioLib's scan re-enters STBY_RC.
// All three call RadioLib internals, so they need -DRADIOLIB_GODMODE=1.
#if (defined(LR11X0_TX_STAGE_EARLY) || defined(LR11X0_CAD_SLIM) || defined(LR11X0_STANDBY_XOSC)) && !RADIOLIB_GODMODE
#error "LR11X0_TX_STAGE_EARLY, LR11X0_CAD_SLIM and LR11X0_STANDBY_XOSC call RadioLib internals: build with -DRADIOLIB_GODMODE=1"
#endif
#if defined(LR11X0_TX_STAGE_EARLY) && !defined(LR11X0_TX_PRESTAGE)
#error "LR11X0_TX_STAGE_EARLY launches through LR11X0_TX_PRESTAGE: build with -DLR11X0_TX_PRESTAGE"
#endif
#if defined(LR11X0_CAD_EXIT_KEYUP) || defined(LR11X0_RESUME_CONTINUOUS_RX)
#define LR11X0_READ_CHIP_MODE 1
#endif

/**
 * \brief Adapter for LR11x0 radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for LR11x0: SX1262, SX1268.
 */
template <class T> class LR11x0Interface : public RadioLibInterface
{
  public:
    LR11x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init() override;

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() override;

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() override;

    bool isIRQPending() override { return lora.getIrqFlags() != 0; }

#if RADIOLIB_GODMODE && defined(MESHTASTIC_RX_FAIL_PROBE)
    /// Bench: the chip's state right after a failed readout. pktType starts at 0xEE, which neither chip uses, so a
    /// GetPacketType that returns success without writing a reply is visible as 0xEE -- distinct from the chip
    /// genuinely reporting NONE (0x00 here), and from it reporting a real modem.
    bool readRxFailState(uint8_t &pktType, uint8_t &mode, uint32_t &irq, int16_t &typeErr) override
    {
        pktType = 0xEE;
        typeErr = lora.getPacketType(&pktType);
#ifdef LR11X0_READ_CHIP_MODE
        mode = this->readChipMode();
#else
        mode = 0xEE; // not compiled in on this board
#endif
        irq = lora.getIrqFlags();
        return true; // always report: a failed GetPacketType is the case of interest, not a reason to stay silent
    }
#endif

#if RADIOLIB_GODMODE
    /// Bench: GetStats, protected in RadioLib. In LoRa the last two are header errors and false syncs (LR1110 user manual).
    bool readChipRxStats(uint16_t &received, uint16_t &crcError, uint16_t &headerError, uint16_t &falseSync) override
    {
        return lora.getStats(&received, &crcError, &headerError, &falseSync) == RADIOLIB_ERR_NONE;
    }
#endif

#ifdef LR11X0_AGC_RESET
    void resetAGC() override;
#endif

  protected:
    /**
     * Specific module instance
     */
    T lora;

    int16_t getCurrentRSSI() override;

    /// Transceiver firmware version as (major << 8 | minor), and which LR11x0 part this is. Captured at
    /// init() from getVersionInfo(); 0 if the query failed.
    uint16_t transceiverFw = 0;
    uint8_t transceiverDevice = 0;

    /**
     * Glue functions called from ISR land
     */
    virtual void clearRadioIsr() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void setRadioIsr(void (*callback)()) override { lora.setIrqAction(callback); }

    /** can we detect a LoRa preamble on the current channel? */
    virtual bool isChannelActive() override;

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving() override;

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive() override;

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend() override;

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(meshtastic_MeshPacket *mp) override;

    virtual void setStandby() override;

    uint32_t getPacketTime(uint32_t pl, bool received) override { return computePacketTime(lora, pl, received); }

    // LR1120/LR1121 work in both bands. 4 sub-GHz is the SWSD003 table's row we use; 8 on 2.4 GHz
    // matches SX1280, the other part sharing that band, so one mesh keeps one CW slot.
    uint8_t getCadSymbolCountSubGhz() const override { return 4; }
    uint8_t getCadSymbolCountWideLora() const override { return 8; }

  private:
    /** Program all modem parameters into the chip; returns the first RadioLib error, or RADIOLIB_ERR_NONE */
    int16_t programModemParams();

    /** Reset and re-begin() a chip that lost its runtime configuration (reset/brownout) */
    bool reinitChip();

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** Forget what the chip was left holding (staged payload, CAD parameters): it is being reset, reprogrammed or slept */
    void forgetChipState();

    /** Bench: -DLR11X0_TCXO_DELAY_US=<us> replaces RadioLib's 5000 us TCXO start-up wait after a successful begin() */
    void applyBenchTcxoDelay(int res);

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }

#ifdef LR11X0_TX_LAUNCH_OVERRIDE
    /** Time the launch from the CAD verdict; with a payload staged before the scan, send only what follows it */
    int16_t launchTransmit(size_t numbytes) override;
    /** The payload isChannelActive() wrote into the chip's buffer before the CAD, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
    /** When the last CAD verdict was read, on the bench clock, for the launch step trace */
    uint32_t cadVerdictClock = 0;
#endif
#ifdef LR11X0_TX_STAGE_EARLY
    bool wantsEarlyTxStage() const override { return true; }
    /** Write the next packet's payload into the TX buffer while RX runs */
    void stageTxEarly(meshtastic_MeshPacket *p) override;
    /** At the scan: true, with the prestage set, if the TX buffer already holds scanForTx's payload */
    bool takeEarlyTxStage();
    /** The payload in the chip's TX buffer, or 0 bytes if it is not known */
    size_t earlyStagedLen = 0;
    uint8_t earlyStagedBytes[256];
    /** Remember the payload just written from radioBuffer */
    void noteTxBuffer(size_t numbytes);
#endif
#ifdef LR11X0_CAD_SLIM
    /** lora.scanChannel(cfg), less what trySetStandby() has just done and the CAD parameters the chip already has */
    int16_t scanChannelForTx(const ChannelScanConfig_t &cfg);
    bool cadParamsValid = false;
    uint8_t cadParamsSent[8];
#endif
#ifdef LR11X0_STANDBY_XOSC
    /** Put the TX/RX fallback on STBY_XOSC, after begin() */
    void keepTcxoOnInStandby();
#endif
#ifdef LR11X0_READ_CHIP_MODE
    /** The chip's mode (stat2 bits 3..1, as RADIOLIB_LR11X0_STAT_2_MODE_*), waiting out a passing FS; 0xFF on SPI failure */
    uint8_t readChipMode() const;
    /** lora's Module, writable from const methods: RadioLib keeps LRxxxx::getStatus() protected, so read status here */
    Module *const statusModule = &module;
#endif
#ifdef LR11X0_CAD_EXIT_KEYUP
    /** A clear CAD under an LBT exit mode put the chip in TX with the prestaged payload: launchTransmit() sends nothing */
    bool chipKeyedUp = false;
#endif
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    /** RX was armed continuous and nothing has put the chip into standby since */
    bool rxArmedContinuous = false;
    bool resumeRunningReceive() override;
    bool receiveStillRunning() const override { return rxArmedContinuous && readChipMode() == RADIOLIB_LR11X0_STAT_2_MODE_RX; }
#endif
#ifdef LR11X0_RX_REARM_AT_TX_DONE
    bool rearmReceiveFromIsr() override;
    void rearmReceiveFromTask() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmState : uint8_t { REARM_NONE, REARM_PENDING, REARM_ARMED, REARM_FAILED };
    volatile uint8_t rearmState = REARM_NONE;
    volatile int16_t rearmErr = 0;
    volatile uint32_t rearmUs = 0;
    /** FreeRTOS tick count when the task finished the re-arm */
    volatile uint32_t rearmTicks = 0;
#endif

    /// The TCXO Vref that init() settled on, so reinitChip() can begin() with the same oscillator setup
    float resolvedTcxoVoltage = 0;
};
#endif