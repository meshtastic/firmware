#pragma once
#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "RadioLibInterface.h"

// After TX_DONE the LR2021 waits in standby for the radio thread to restart RX, which a main-loop hold can stretch. With the
// readout task, the TX_DONE interrupt has the task restart it instead; -DLR2021_RX_REARM_AT_TX_DONE=0 opts out.
#if defined(MESHTASTIC_RX_READOUT_TASK) && !defined(LR2021_RX_REARM_AT_TX_DONE)
#define LR2021_RX_REARM_AT_TX_DONE 1
#endif

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

#if defined(MESHTASTIC_RX_READOUT_TASK) && LR2021_RX_REARM_AT_TX_DONE
    bool rearmReceiveFromIsr() override;
    void rearmReceiveFromTask() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmState : uint8_t { REARM_NONE, REARM_PENDING, REARM_ARMED, REARM_FAILED };
    volatile uint8_t rearmState = REARM_NONE;
    volatile int16_t rearmErr = 0;
#endif

    /** Recover a chip that lost its runtime state via the same full begin() the band-hop path uses */
    bool recoverChipStateLoss() override { return fullBegin(getFreq()); }

#ifdef LR2021_LOAD_PRAM
    /** Load and activate Semtech's patch RAM, then re-apply the modem settings begin() made; RadioLib status */
    int16_t loadPram();
#endif
};
#endif
