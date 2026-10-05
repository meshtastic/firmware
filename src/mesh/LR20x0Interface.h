#pragma once
#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "RadioLibInterface.h"

// After TX_DONE the LR2021 waits in standby for the radio thread to restart RX, which a main-loop hold can stretch. With the
// readout task, the TX_DONE interrupt has the task restart it instead.
//
// Restarting RX after a frame is a standby and the whole RX setup again, deaf throughout, where a continuous RX is still
// listening. We keep that RX running instead, checking the chip is still in RX first.

// RadioLib 7.8 replaced the LR2021's setRegMode(simo, rampTimes) with setRegMode(simo) and the public setRegulatorDCDC(), and
// applies Semtech's DCDC sensitivity workaround itself on every modulation change.
#if (RADIOLIB_VERSION_MAJOR > 7) || (RADIOLIB_VERSION_MAJOR == 7 && RADIOLIB_VERSION_MINOR >= 8)
#define LR2021_RADIOLIB_HAS_DCDC 1
#endif
// -DLR2021_REGULATOR_LDO keeps the chip's default LDO regulator instead of the DC-DC (SIMO) one
#if defined(LR2021_REGULATOR_LDO) && !defined(LR2021_RADIOLIB_HAS_DCDC)
#error "LR2021_REGULATOR_LDO needs RadioLib 7.8 or later"
#endif
// -DLR2021_LOAD_PRAM loads Semtech's LR2021 patch RAM after every chip reset. Semtech's driver says the PRAM fixes, among
// others, the DC-DC (SIMO) regulator's cost to sub-GHz LoRa sensitivity. It writes chip memory directly, so it needs
// -DRADIOLIB_GODMODE=1, and RadioLib 7.8, whose WriteRegMem32 no longer sends a stray byte after the data.
#if defined(LR2021_LOAD_PRAM) && (!RADIOLIB_GODMODE || !defined(LR2021_RADIOLIB_HAS_DCDC))
#error "LR2021_LOAD_PRAM writes chip memory directly: build with -DRADIOLIB_GODMODE=1 and RadioLib 7.8 or later"
#endif
// -DLR2021_STANDBY_XOSC keeps the TCXO running: standby and the RX/TX fallback are STBY_XOSC, and the scan starts from there
// without RadioLib's STBY_RC. It calls RadioLib internals, so it needs -DRADIOLIB_GODMODE=1.
#if defined(LR2021_STANDBY_XOSC) && !RADIOLIB_GODMODE
#error "LR2021_STANDBY_XOSC calls RadioLib internals: build with -DRADIOLIB_GODMODE=1"
#endif
// -DLR2021_TX_PRESTAGE writes the TX payload into the chip's TX FIFO before the channel scan, so a clear verdict sends only
// packet params, IRQ setup and SET_TX. It calls RadioLib's LR2021 commands directly, so it needs -DRADIOLIB_GODMODE=1.
// -DLR2021_PRESTAGE_UPSTREAM stages through RadioLib's own prestageTransmit() (RadioLib #1883) instead, whose stageMode(TX)
// skips the FIFO write for the payload it staged; that needs no GODMODE.
#if defined(LR2021_TX_PRESTAGE) && !RADIOLIB_GODMODE && !defined(LR2021_PRESTAGE_UPSTREAM)
#error "LR2021_TX_PRESTAGE calls RadioLib's LR2021 commands directly: build with -DRADIOLIB_GODMODE=1"
#endif
#if defined(LR2021_PRESTAGE_UPSTREAM) && !defined(LR2021_TX_PRESTAGE)
#error "LR2021_PRESTAGE_UPSTREAM changes how LR2021_TX_PRESTAGE stages: build with -DLR2021_TX_PRESTAGE"
#endif
// -DLR2021_CAD_EXIT_LBT scans with CAD exit mode LBT: a clear CAD keys up from the prestaged payload, and a busy one leaves the
// chip in standby for rearmReceive() to restart RX, with no CAD>RX handoff. The chip keys up without the MCU, so the RF
// switch must be the chip's DIOs or none.
#if defined(LR2021_CAD_EXIT_LBT) && (!defined(LR2021_TX_PRESTAGE) || !RADIOLIB_GODMODE)
#error "LR2021_CAD_EXIT_LBT sends the prestaged payload: build with -DLR2021_TX_PRESTAGE -DRADIOLIB_GODMODE=1"
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
    bool resetAGC() override;
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

    bool readRxHeaderInfo(uint8_t &cr, bool &hasCRC) override
    {
        return lora.getLoRaRxHeaderInfo(&cr, &hasCRC) == RADIOLIB_ERR_NONE;
    }

    // LR2021 works in both bands. 4 sub-GHz is Table 6-19's row we use; 8 on 2.4 GHz matches SX1280,
    // the other part sharing that band, so one mesh keeps one CW slot.
    uint8_t getCadSymbolCountSubGhz() const override { return 4; }
    uint8_t getCadSymbolCountWideLora() const override { return 8; }

  private:
    /** Chip-side re-init shared by the band-hop and recovery paths: front-end GPIOs, begin(), CRC, RF switch, RX gain */
    bool fullBegin(float freq);

    /** Board LF PA table after begin(); HF keeps RadioLib default. Warn-only on setOutputPower miss. */
    void applyCustomLfPaTable(float freq);

    /** Point RadioLib at the board LF PA table inside 500-1000 MHz, back at its default outside it. */
    void selectLfPaTable(float freq);

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

#ifdef MESHTASTIC_RX_READOUT_TASK
    bool rearmReceiveFromIsr() override;
    void rearmReceiveFromTask() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmState : uint8_t { REARM_NONE, REARM_PENDING, REARM_ARMED, REARM_FAILED };
    volatile uint8_t rearmState = REARM_NONE;
    volatile int16_t rearmErr = 0;
#endif

    /** The chip's mode (stat2 bits 2..0, as the LR20X0_CHIP_MODE_* below), waiting out a passing FS; 0xFF on SPI failure */
    uint8_t readChipMode();
    static constexpr uint8_t LR20X0_CHIP_MODE_STBY_RC = 1;
    static constexpr uint8_t LR20X0_CHIP_MODE_STBY_XOSC = 2;
    static constexpr uint8_t LR20X0_CHIP_MODE_FS = 3;
    static constexpr uint8_t LR20X0_CHIP_MODE_RX = 4;
    static constexpr uint8_t LR20X0_CHIP_MODE_TX = 5;

    /** RX was armed continuous and nothing has put the chip into standby since */
    bool rxArmedContinuous = false;
    bool resumeRunningReceive() override;

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

#ifdef LR2021_TX_PRESTAGE
    /** With a payload staged before the scan, send only what follows it */
    int16_t launchTransmit(size_t numbytes) override;
    /** The payload isChannelActive() wrote into the chip's TX FIFO before the scan, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
#ifndef LR2021_PRESTAGE_UPSTREAM
    /** The TX FIFO may hold bytes no TX has sent. It appends, so they would go out ahead of the next payload */
    bool txFifoStale = true;
    /** Empty the TX FIFO if it may hold unsent bytes */
    int16_t clearStaleTxFifo();
#endif
#endif

#ifdef LR2021_CAD_EXIT_LBT
    /** A clear CAD under exit mode LBT put the chip in TX with the prestaged payload: launchTransmit() sends nothing */
    bool chipKeyedUp = false;
#endif

#ifdef LR2021_LOAD_PRAM
    /** Load and activate Semtech's patch RAM, then re-apply the modem settings begin() made; RadioLib status */
    int16_t loadPram();
#endif
};
#endif
