#pragma once
#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "RadioLibInterface.h"

// Bench: -DLR2021_TX_LAUNCH_TRACE times each step from the CAD verdict to TX in microseconds, and -DLR2021_TX_PRESTAGE also
// writes the payload into the TX FIFO before the scan. Prestage calls RadioLib's LR2021 commands directly, so it needs
// -DRADIOLIB_GODMODE=1.
#if defined(LR2021_TX_LAUNCH_TRACE) || defined(LR2021_TX_PRESTAGE)
#define LR2021_TX_LAUNCH_OVERRIDE 1
#endif
#if defined(LR2021_TX_PRESTAGE) && !RADIOLIB_GODMODE
#error "LR2021_TX_PRESTAGE calls RadioLib's LR2021 commands directly: build with -DRADIOLIB_GODMODE=1"
#endif
// Bench: -DLR2021_CAD_EXIT_LBT scans with CAD exit mode TX: a clear CAD keys up from the prestaged payload, and a busy one
// leaves the chip in its fallback standby for rearmReceive() to restart RX, with no CAD>RX handoff.
#if defined(LR2021_CAD_EXIT_LBT) && !defined(LR2021_TX_PRESTAGE)
#error "LR2021_CAD_EXIT_LBT sends the prestaged payload: build with -DLR2021_TX_PRESTAGE -DRADIOLIB_GODMODE=1"
#endif
// Bench: -DLR2021_RESUME_CONTINUOUS_RX keeps a continuous RX running after a frame instead of restarting it, checking the
// chip is still in RX first.
// Bench: -DLR2021_STANDBY_XOSC keeps the TCXO running: standby is STBY_XOSC, the RX/TX fallback (where a CAD exits when it
// hands off to neither RX nor TX) is STBY_XOSC, and the scan starts from there without RadioLib's STBY_RC. It calls RadioLib
// internals, so it needs -DRADIOLIB_GODMODE=1.
#if defined(LR2021_STANDBY_XOSC) && !RADIOLIB_GODMODE
#error "LR2021_STANDBY_XOSC calls RadioLib internals: build with -DRADIOLIB_GODMODE=1"
#endif
#if defined(LR2021_CAD_EXIT_LBT) || defined(LR2021_RESUME_CONTINUOUS_RX)
#define LR2021_READ_CHIP_MODE 1
#endif

/**
 * \brief Adapter for LR20x0 radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for LR20x0, e.g. LR2021.
 */
template <class T> class LR20x0Interface : public RadioLibInterface
{
  public:
    LR20x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
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

#ifdef LR20X0_AGC_RESET
    void resetAGC() override;
#endif

  protected:
    /**
     * Specific module instance
     */
    T lora;

    int16_t getCurrentRSSI() override;

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

    /**
     * Apply the Semtech DCDC sensitivity workaround (opt-in, godmode-only). Must be called after the LoRa
     * modulation parameters have been set - i.e. after lora.begin() in init(), or after the
     * setSpreadingFactor/setBandwidth/setCodingRate calls in reconfigure(), all of which re-run
     * setLoRaModulationParams() and thereby reset the DCDC configure state. No-op unless built with
     * -DLR2021_DCDC_WORKAROUND (and RADIOLIB_GODMODE). Logs success/failure; never fatal.
     */
    void applyDcdcWorkaround();

    uint32_t getPacketTime(uint32_t pl, bool received) override { return computePacketTime(lora, pl, received); }

    // LR2021 works in both bands. 4 sub-GHz is Table 6-19's row we use; 8 on 2.4 GHz matches SX1280,
    // the other part sharing that band, so one mesh keeps one CW slot.
    uint8_t getCadSymbolCountSubGhz() const override { return 4; }
    uint8_t getCadSymbolCountWideLora() const override { return 8; }

  private:
    /** Chip-side re-init shared by the band-hop and recovery paths: front-end GPIOs, begin(), CRC, RF switch, RX gain */
    bool fullBegin(float freq);

    /** Board LF PA table after begin(); HF keeps RadioLib default. Warn-only on setOutputPower miss. */
    void applyCustomLfPaTable(float freq);

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** Recover a chip that lost its runtime state via the same full begin() the band-hop path uses */
    bool recoverChipStateLoss() override { return fullBegin(getFreq()); }

    /** SetStandby's oscillator: STBY_XOSC with -DLR2021_STANDBY_XOSC, else STBY_RC as RadioLib's standby() */
#ifdef LR2021_STANDBY_XOSC
    static constexpr uint8_t STANDBY_MODE = RADIOLIB_LR2021_STANDBY_XOSC;
    /** Put the TX/RX fallback on STBY_XOSC, after begin() */
    void keepTcxoOnInStandby();
    /** lora.scanChannel(cfg) without its STBY_RC: trySetStandby() has just put the chip in STBY_XOSC */
    int16_t scanChannelFromStandby(const ChannelScanConfig_t &cfg);
#else
    static constexpr uint8_t STANDBY_MODE = RADIOLIB_LR2021_STANDBY_RC;
#endif

#ifdef LR2021_TX_LAUNCH_OVERRIDE
    /** Time the launch from the CAD verdict; with a payload staged before the scan, send only what follows it */
    int16_t launchTransmit(size_t numbytes) override;
    /** The payload isChannelActive() wrote into the chip's TX FIFO before the CAD, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
    /** When the last CAD verdict was read, on the bench clock, for the launch step trace */
    uint32_t cadVerdictClock = 0;
#endif
#ifdef LR2021_TX_PRESTAGE
    /** The TX FIFO may hold bytes no TX has sent. It appends, so they would go out ahead of the next payload */
    bool txFifoStale = true;
    /** Empty the TX FIFO if it may hold unsent bytes */
    int16_t clearStaleTxFifo();
#endif
#ifdef LR2021_READ_CHIP_MODE
    /** The chip's mode (stat2 bits 2..0, as the LR20X0_CHIP_MODE_* below), waiting out a passing FS; 0xFF on SPI failure */
    uint8_t readChipMode() const;
    /** lora's Module, writable from const methods: RadioLib's LRxxxx::getStatus() is protected, so read status here */
    Module *const statusModule = &module;
    static constexpr uint8_t LR20X0_CHIP_MODE_STBY_RC = 1;
    static constexpr uint8_t LR20X0_CHIP_MODE_STBY_XOSC = 2;
    static constexpr uint8_t LR20X0_CHIP_MODE_FS = 3;
    static constexpr uint8_t LR20X0_CHIP_MODE_RX = 4;
    static constexpr uint8_t LR20X0_CHIP_MODE_TX = 5;
#endif
#ifdef LR2021_CAD_EXIT_LBT
    /** A clear CAD under exit mode TX put the chip in TX with the prestaged payload: launchTransmit() sends nothing */
    bool chipKeyedUp = false;
#endif
#ifdef LR2021_RESUME_CONTINUOUS_RX
    /** RX was armed continuous and nothing has put the chip into standby since */
    bool rxArmedContinuous = false;
    bool resumeRunningReceive() override;
    bool receiveStillRunning() const override { return rxArmedContinuous && readChipMode() == LR20X0_CHIP_MODE_RX; }
#endif
};
#endif
