#pragma once
#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "RadioLibInterface.h"

// Write the TX payload into the chip before the channel scan, so a clear verdict sends only packet params, IRQ setup
// and SET_TX. It sets RadioLib's staged mode directly, so it is on where RADIOLIB_GODMODE is; 0 opts out.
#ifndef LR11X0_TX_PRESTAGE
#define LR11X0_TX_PRESTAGE RADIOLIB_GODMODE
#endif
#if LR11X0_TX_PRESTAGE && !RADIOLIB_GODMODE
#error "LR11X0_TX_PRESTAGE sets RadioLib's staged mode directly: build with -DRADIOLIB_GODMODE=1"
#endif

// Options that shorten the scan's deaf window, each off unless the build sets it. They call RadioLib internals, so each
// needs RADIOLIB_GODMODE.
// LR11X0_TX_STAGE_EARLY: write the payload during the backoff while RX runs, not in the scan's standby. WriteBuffer8
// fills the TX buffer and received frames land in the separate RX buffer (Semtech's lr11xx_regmem.h), so the write
// aborts no frame and nothing received overwrites it.
#ifndef LR11X0_TX_STAGE_EARLY
#define LR11X0_TX_STAGE_EARLY 0
#endif
// LR11X0_CAD_SLIM: start the scan without RadioLib's packet-type reads and second standby, and send the CAD
// parameters only when they change.
#ifndef LR11X0_CAD_SLIM
#define LR11X0_CAD_SLIM 0
#endif
// LR11X0_STANDBY_XOSC: standby and the TX/RX fallback keep the TCXO running, so a CAD or RX started from them skips
// the TCXO start-up. It reaches the scan only with LR11X0_CAD_SLIM: RadioLib's scan re-enters STBY_RC.
#ifndef LR11X0_STANDBY_XOSC
#define LR11X0_STANDBY_XOSC 0
#endif
#if (LR11X0_TX_STAGE_EARLY || LR11X0_CAD_SLIM || LR11X0_STANDBY_XOSC) && !RADIOLIB_GODMODE
#error "LR11X0_TX_STAGE_EARLY, LR11X0_CAD_SLIM and LR11X0_STANDBY_XOSC call RadioLib internals: build with -DRADIOLIB_GODMODE=1"
#endif
#if LR11X0_TX_STAGE_EARLY && !LR11X0_TX_PRESTAGE
#error "LR11X0_TX_STAGE_EARLY launches through LR11X0_TX_PRESTAGE"
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

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }

#if LR11X0_TX_PRESTAGE
    /** With a payload staged before the scan, send only what follows it */
    int16_t launchTransmit(size_t numbytes) override;
    /** The payload isChannelActive() wrote into the chip's buffer before the scan, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
#endif

#if LR11X0_TX_STAGE_EARLY
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

#if LR11X0_CAD_SLIM
    /** lora.scanChannel(cfg), less what trySetStandby() has just done and the CAD parameters the chip already has */
    int16_t scanChannelForTx(const ChannelScanConfig_t &cfg);
    bool cadParamsValid = false;
    uint8_t cadParamsSent[8];
#endif

#if LR11X0_STANDBY_XOSC
    /** Put the TX/RX fallback on STBY_XOSC, after begin() */
    void keepTcxoOnInStandby();
#endif

    /// The TCXO Vref that init() settled on, so reinitChip() can begin() with the same oscillator setup
    float resolvedTcxoVoltage = 0;
};
#endif