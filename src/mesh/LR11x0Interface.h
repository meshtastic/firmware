#pragma once
#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "RadioLibInterface.h"

// After TX_DONE the LR11x0 waits in standby for the radio thread to restart RX, which a main-loop hold can stretch by
// hundreds of ms. With the readout task, the TX_DONE interrupt has the task restart it instead.
//
// Restarting RX after a frame is a standby and the whole RX setup again, deaf throughout, where a continuous RX is still
// listening. We pick that RX back up instead, once the chip reports it is still in RX.

// Write the TX payload into the chip before the channel scan, so a clear verdict sends only packet params, IRQ setup
// and SET_TX. It sets RadioLib's staged mode directly, so it is on where RADIOLIB_GODMODE is; 0 opts out.
#ifndef LR11X0_TX_PRESTAGE
#define LR11X0_TX_PRESTAGE RADIOLIB_GODMODE
#endif
#if LR11X0_TX_PRESTAGE && !RADIOLIB_GODMODE
#error "LR11X0_TX_PRESTAGE sets RadioLib's staged mode directly: build with -DRADIOLIB_GODMODE=1"
#endif
// -DLR11X0_STANDBY_XOSC keeps the TCXO running in standby and in the TX/RX fallback, so a CAD, RX or TX started from them
// skips its start-up. RadioLib's own scan still drops to STBY_RC before the CAD.
#if defined(LR11X0_STANDBY_XOSC) && !RADIOLIB_GODMODE
#error "LR11X0_STANDBY_XOSC sets the TX/RX fallback mode directly: build with -DRADIOLIB_GODMODE=1"
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
    bool resetAGC() override;
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

    bool readRxHeaderInfo(uint8_t &cr, bool &hasCRC) override
    {
        return lora.getLoRaRxHeaderInfo(&cr, &hasCRC) == RADIOLIB_ERR_NONE;
    }

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

#ifdef MESHTASTIC_RX_READOUT_TASK
    bool rearmReceiveFromIsr() override;
    void rearmReceiveFromTask() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmState : uint8_t { REARM_NONE, REARM_PENDING, REARM_ARMED, REARM_FAILED };
    volatile uint8_t rearmState = REARM_NONE;
    volatile int16_t rearmErr = 0;
#endif

    /** The chip's mode (stat2 bits 3..1, as RADIOLIB_LR11X0_STAT_2_MODE_*), waiting out a passing FS; 0xFF on failure */
    uint8_t readChipMode();

    /** RX was armed continuous and nothing has taken the chip out of it since, so it is still listening */
    bool rxArmedContinuous = false;
    bool resumeRunningReceive() override;

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }

#ifdef LR11X0_STANDBY_XOSC
    /** Put the TX/RX fallback on STBY_XOSC; begin() resets it */
    void keepTcxoOnInStandby();
#endif

#if LR11X0_TX_PRESTAGE
    /** With a payload staged before the scan, send only what follows it */
    int16_t launchTransmit(size_t numbytes) override;
    /** The payload isChannelActive() wrote into the chip's buffer before the scan, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
#endif

    /// The TCXO Vref that init() settled on, so reinitChip() can begin() with the same oscillator setup
    float resolvedTcxoVoltage = 0;
};
#endif